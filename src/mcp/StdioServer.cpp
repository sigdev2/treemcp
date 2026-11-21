#include "StdioServer.hpp"

#include "Framer.hpp"
#include "Session.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QTimer>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <unistd.h>
#endif

namespace treemcp::StdioServer {
namespace {

using Buffer = std::array<char, 8192>;
using namespace std::chrono_literals;

// Interrupt only the thread that owns the blocking native operation. Callers retry
// until it exits, covering an interrupt arriving just before the system call.
class IoThread {
public:
    void start(std::function<void()> run)
    {
        thread = std::thread([this, run = std::move(run)] {
#ifdef Q_OS_WIN
            handle.store(OpenThread(THREAD_TERMINATE, FALSE, GetCurrentThreadId()));
            if (!handle.load()) {
                stopped.store(true);
            }
#else
            sigset_t signalSet;
            sigemptyset(&signalSet);
            sigaddset(&signalSet, SIGUSR1);
            pthread_sigmask(SIG_UNBLOCK, &signalSet, nullptr);
#endif
            run();
        });
    }

    void interrupt()
    {
        stopped.store(true);
#ifdef Q_OS_WIN
        if (const auto target = handle.load()) {
            CancelSynchronousIo(target);
        }
#else
        pthread_kill(thread.native_handle(), SIGUSR1);
#endif
    }

    void join()
    {
        thread.join();
#ifdef Q_OS_WIN
        if (const auto target = handle.exchange(nullptr)) {
            CloseHandle(target);
        }
#endif
    }

    std::atomic_bool stopped = false;

private:
    std::thread thread;
#ifdef Q_OS_WIN
    std::atomic<HANDLE> handle = nullptr;
#endif
};

qsizetype readInput(Buffer& buffer, const IoThread& reader)
{
    while (!reader.stopped.load()) {
#ifdef Q_OS_WIN
        DWORD count = 0;
        if (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
        {
            return count;
        }
        const DWORD error = GetLastError();
        return error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF ? 0 : -1;
#else
        const auto count = read(STDIN_FILENO, buffer.data(), buffer.size());
        if (count >= 0 || errno != EINTR) {
            return count;
        }
#endif
    }
    return -1;
}

qsizetype writeChunk(QByteArrayView bytes, const IoThread& writer)
{
    bytes = bytes.first(std::min<qsizetype>(16384, bytes.size()));
    while (!writer.stopped.load()) {
#ifdef Q_OS_WIN
        DWORD count = 0;
        if (WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr))
        {
            return count;
        }
        return -1;
#else
        const auto count = write(STDOUT_FILENO, bytes.data(), static_cast<size_t>(bytes.size()));
        if (count >= 0 || errno != EINTR) {
            return count;
        }
#endif
    }
    return -1;
}

class Output {
public:
    explicit Output(const std::function<void()>& onFailure)
    {
        writer.start([this, onFailure] {
            std::unique_lock lock(mutex);
            for (;;) {
                changed.wait(lock, [&] { return closing || !lines.empty(); });
                if (lines.empty()) {
                    break;
                }
                const auto line = std::move(lines.front());
                lines.pop_front();
                QByteArrayView remaining(line);
                while (!remaining.empty()) {
                    lock.unlock();
                    const auto count = writeChunk(remaining, writer);
                    lock.lock();
                    if (count <= 0) {
                        done = true;
                        failed = true;
                        changed.notify_all();
                        onFailure();
                        return;
                    }
                    remaining = remaining.sliced(count);
                    ++progress;
                    if (closing) {
                        changed.notify_all();
                    }
                }
                pendingBytes -= line.size();
            }
            done = true;
            changed.notify_all();
        });
    }

    bool publish(const QJsonObject& reply)
    {
        auto line = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
        std::lock_guard lock(mutex);
        // Includes the record currently being written.
        if (failed || pendingBytes + line.size() > 16 * 1024 * 1024) {
            return false;
        }
        pendingBytes += line.size();
        lines.push_back(std::move(line));
        changed.notify_one();
        return true;
    }

    bool finish()
    {
        std::unique_lock lock(mutex);
        closing = true;
        changed.notify_all();
        while (!done) {
            const auto previous = progress;
            if (!changed.wait_for(lock, failed ? 20ms : 1000ms, [&] { return done || previous != progress; })) {
                failed = true;
                writer.interrupt();
            }
        }
        lock.unlock();
        writer.join();
        return !failed;
    }

private:
    IoThread writer;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<QByteArray> lines;
    qsizetype pendingBytes = 0;
    quint64 progress = 0;
    bool closing = false;
    bool done = false;
    bool failed = false;
};

} // namespace

int run(QCoreApplication& application, const Workspace& workspace, int workers)
{
#ifndef Q_OS_WIN
    std::signal(SIGPIPE, SIG_IGN);
    struct sigaction action {};
    action.sa_handler = [](int) {};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, nullptr) != 0) {
        return 1;
    }
#endif
    IoThread reader;
    QTimer interruptReader;
    interruptReader.setInterval(20);
    QObject::connect(&interruptReader, &QTimer::timeout, &application, [&] { reader.interrupt(); });
    const auto fail = [&] {
        reader.interrupt();
        interruptReader.start();
    };
    Output output([&] { QMetaObject::invokeMethod(&interruptReader, fail, Qt::QueuedConnection); });
    const auto publish = [&](const QJsonObject& reply) {
        if (!output.publish(reply)) {
            fail();
        }
    };
    Session session(workspace, workers, publish);
    Framer framer;
    const Framer::Receiver receive = [&](QByteArrayView frame) {
        if (const auto reply = session.handle(frame)) {
            publish(*reply);
        }
        return !reader.stopped.load();
    };

    // The blocking input handoff bounds the queue to one chunk. Only the writer waits for stdout.
    reader.start([&] {
        Buffer buffer;
        bool ok = true;
        qsizetype count;
        do {
            count = readInput(buffer, reader);
            if (count < 0) {
                ok = false;
                break;
            }
            QMetaObject::invokeMethod(
                &application,
                [&] {
                    if (count == 0) {
                        session.shutdown();
                        ok = framer.finish(receive);
                    } else {
                        ok = framer.feed({ buffer.data(), count }, receive);
                    }
                },
                Qt::BlockingQueuedConnection);
        } while (ok && count > 0);
        QMetaObject::invokeMethod(
            &application,
            [ok] { QCoreApplication::exit(ok ? 0 : 1); },
            Qt::QueuedConnection);
    });
    const int status = application.exec();
    interruptReader.stop();
    session.shutdown();
    reader.join();
    const bool drained = output.finish();
    if (status || reader.stopped.load() || !drained) {
        std::fputs("MCP stdio operation failed.\n", stderr);
        return 1;
    }
    return 0;
}

} // namespace treemcp::StdioServer
