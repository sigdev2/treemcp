#include "StdioServer.hpp"

#include "Framer.hpp"
#include "Session.hpp"

#include <QCoreApplication>
#include <QJsonDocument>

#include <array>
#include <cstdio>
#include <thread>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace treemcp::StdioServer {
namespace {

using Buffer = std::array<char, 8192>;

qsizetype readInput(Buffer& buffer)
{
#ifdef Q_OS_WIN
    DWORD count = 0;
    if (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) {
        return count;
    }
    const DWORD errorCode = GetLastError();
    return errorCode == ERROR_BROKEN_PIPE || errorCode == ERROR_HANDLE_EOF ? 0 : -1;
#else
    ssize_t count;
    do {
        count = read(STDIN_FILENO, buffer.data(), buffer.size());
    } while (count < 0 && errno == EINTR);
    return count;
#endif
}

bool writeReply(const QJsonObject& reply)
{
    const QByteArray line = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
    const size_t size = static_cast<size_t>(line.size());
    return std::fwrite(line.constData(), 1, size, stdout) == size && std::fflush(stdout) == 0;
}

} // namespace

int run(QCoreApplication& application)
{
#ifdef Q_OS_WIN
    if (_setmode(_fileno(stdout), _O_BINARY) < 0) {
        std::fputs("Cannot configure stdout for MCP.\n", stderr);
        return 1;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    Session session;
    Framer framer;
    const Framer::Receiver receive = [&session](QByteArrayView frame) {
        const auto reply = session.handle(frame);
        return !reply || writeReply(*reply);
    };
    // A blocking handoff keeps the event loop responsive while stdin is idle and bounds the queue
    // to one chunk. The reader owns the buffer until the main thread finishes consuming it.
    std::thread reader([&application, &framer, &receive]() {
        Buffer buffer;
        bool ok = true;
        qsizetype count;
        do {
            count = readInput(buffer);
            if (count < 0) {
                ok = false;
                break;
            }
            QMetaObject::invokeMethod(
                &application,
                [&buffer, count, &framer, &receive, &ok]() {
                    ok = count == 0 ? framer.finish(receive) : framer.feed({ buffer.data(), count }, receive);
                },
                Qt::BlockingQueuedConnection);
        } while (ok && count > 0);
        QMetaObject::invokeMethod(
            &application,
            [ok]() {
                if (!ok) {
                    std::fputs("MCP stdio operation failed.\n", stderr);
                }
                QCoreApplication::exit(ok ? 0 : 1);
            },
            Qt::QueuedConnection);
    });
    const int status = application.exec();
    reader.join();
    return status;
}

} // namespace treemcp::StdioServer
