#include "Scheduler.hpp"

#include <QSemaphore>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

bool checkFairness()
{
    QSemaphore started;
    QSemaphore resume;
    QSemaphore finished;
    std::vector<int> order;
    const std::atomic_bool cancelled = false;
    treemcp::Scheduler scheduler(2);
    treemcp::FileJobs files;
    for (int i = 0; i < 2; ++i) {
        scheduler.submit(i, [&] {
            started.release();
            resume.acquire();
        });
    }
    started.acquire(2);
    scheduler.submit(2, [&] { order.push_back(2); });
    scheduler.submit(3, [&] { order.push_back(3); });
    scheduler.submit(4, [&] {
        order.push_back(4);
        finished.release();
    });
    const treemcp::SearchStop stop { cancelled, Clock::time_point::max() };
    scheduler.submitFile(files, stop, [&] { order.push_back(1); });
    scheduler.submitFile(files, stop, [&] { order.push_back(5); });
    const bool removed = scheduler.remove(3);
    resume.release(); // Keep the second worker parked so the dequeue order is observable.
    finished.acquire();
    scheduler.joinFiles(files);
    resume.release();
    scheduler.shutdown();
    return removed && order == std::vector<int> { 1, 2, 5, 4 };
}

bool checkCapacity()
{
    QSemaphore started;
    QSemaphore resume;
    const std::atomic_bool cancelled = false;
    treemcp::Scheduler scheduler(2);
    treemcp::FileJobs files;
    const auto file = [&] {
        started.release();
        resume.acquire();
    };
    for (int i = 0; i < 2; ++i) {
        scheduler.submitFile(files, { cancelled, Clock::time_point::max() }, file);
    }
    started.acquire(2); // One batch can occupy both workers.
    const bool overflow =
        scheduler.submitFile(files, { cancelled, Clock::now() + std::chrono::milliseconds(20) }, [] {});
    std::atomic_bool single = false;
    scheduler.submit(1, [&] { single = true; });
    const bool bounded = !single.load();
    resume.release(2);
    scheduler.joinFiles(files);
    scheduler.shutdown();
    return !overflow && bounded && single.load();
}

bool checkCancelledChildren()
{
    treemcp::Scheduler scheduler(2);
    treemcp::FileJobs files;
    QSemaphore started;
    QSemaphore resumeSingle;
    QSemaphore resumeFile;
    std::atomic_bool cancelled = false;
    const treemcp::SearchStop stop { cancelled, Clock::time_point::max() };
    auto runningState = std::make_shared<int>(1);
    auto queuedState = std::make_shared<int>(2);
    const std::weak_ptr<int> running = runningState;
    const std::weak_ptr<int> queued = queuedState;
    std::atomic_bool queuedRan = false;
    scheduler.submit(1, [&] {
        started.release();
        resumeSingle.acquire();
    });
    scheduler.submitFile(files, stop, [&, state = std::move(runningState)] {
        started.release();
        resumeFile.acquire();
    });
    started.acquire(2);
    scheduler.submitFile(files, stop, [&, state = std::move(queuedState)] { queuedRan = true; });
    cancelled = true;
    scheduler.cancelFiles(files);
    const bool removed = queued.expired() && !running.expired();
    resumeFile.release();
    scheduler.joinFiles(files);
    const bool released = running.expired() && !queuedRan;
    resumeSingle.release();
    scheduler.shutdown();
    if (removed && released) {
        std::puts("Cancelled queued child released immediately; running child's state released before join returned");
    }
    return removed && released;
}

bool checkOwnerIsolation()
{
    treemcp::Scheduler scheduler(2);
    treemcp::FileJobs first;
    treemcp::FileJobs second;
    const std::atomic_bool cancelled = false;
    const treemcp::SearchStop stop { cancelled, Clock::time_point::max() };
    QSemaphore parked;
    QSemaphore releaseSingle;
    QSemaphore releaseFirst;
    QSemaphore releaseSecond;
    QSemaphore secondStarted;
    scheduler.submit(1, [&] {
        parked.release();
        releaseSingle.acquire();
    });
    scheduler.submitFile(first, stop, [&] {
        parked.release();
        releaseFirst.acquire();
    });
    parked.acquire(2);
    scheduler.submitFile(second, stop, [&] {
        secondStarted.release();
        releaseSecond.acquire();
    });
    QSemaphore submitting;
    bool submitted = true;
    std::thread blocked([&] {
        submitting.release();
        submitted = scheduler.submitFile(first, stop, [] {});
    });
    submitting.acquire();
    scheduler.cancelFiles(first);
    blocked.join();
    releaseFirst.release();
    scheduler.joinFiles(first);
    const bool unaffected = secondStarted.tryAcquire(1, 1000);
    releaseSecond.release();
    releaseSingle.release();
    scheduler.joinFiles(second);
    scheduler.shutdown();
    return !submitted && unaffected;
}

bool checkReservation()
{
    treemcp::Scheduler scheduler(2);
    treemcp::FileJobs files;
    const std::atomic_bool cancelled = false;
    const treemcp::SearchStop stop { cancelled, Clock::time_point::max() };
    QSemaphore started;
    QSemaphore resume;
    scheduler.reserveWorkerForSingles(true);
    scheduler.submitFile(files, stop, [&] {
        started.release();
        resume.acquire();
    });
    started.acquire();
    QSemaphore anotherFile;
    QSemaphore single;
    scheduler.submit(1, [&] { single.release(); });
    const treemcp::SearchStop deadline { cancelled, Clock::now() + std::chrono::milliseconds(20) };
    const bool blocked = !scheduler.submitFile(files, deadline, [] {});
    const bool reserved = single.tryAcquire(1, 1000);
    scheduler.reserveWorkerForSingles(false);
    const bool released =
        scheduler.submitFile(files, stop, [&] { anotherFile.release(); }) && anotherFile.tryAcquire(1, 1000);
    resume.release();
    scheduler.joinFiles(files);
    scheduler.shutdown();
    return blocked && reserved && released;
}

bool checkInlineCapacity()
{
    treemcp::Scheduler scheduler(1);
    treemcp::FileJobs files;
    const std::atomic_bool cancelled = false;
    const treemcp::SearchStop stop { cancelled, Clock::time_point::max() };
    QSemaphore started;
    QSemaphore release;
    bool overlapped = false;
    const bool ran = scheduler.tryRunFileInline(files, stop, [&] {
        scheduler.submit(1, [&] {
            started.release();
            release.acquire();
        });
        overlapped = started.tryAcquire(1, 50);
    });
    const bool workerStarted = overlapped || started.tryAcquire(1, 1000);
    const bool ranWhileOccupied = scheduler.tryRunFileInline(files, stop, [] {});
    release.release();
    scheduler.shutdown();
    return ran && workerStarted && !overlapped && !ranWhileOccupied;
}

} // namespace

int main()
{
    return checkFairness() && checkCapacity() && checkCancelledChildren() && checkOwnerIsolation() &&
            checkReservation() && checkInlineCapacity()
        ? 0
        : 1;
}
