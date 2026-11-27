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
    scheduler.submitFile(stop, [&] { order.push_back(1); });
    scheduler.submitFile(stop, [&] { order.push_back(5); });
    const bool removed = scheduler.remove(3);
    resume.release(); // Keep the second worker parked so the dequeue order is observable.
    finished.acquire();
    scheduler.joinFiles();
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
    const auto file = [&] {
        started.release();
        resume.acquire();
    };
    for (int i = 0; i < 2; ++i) {
        scheduler.submitFile({ cancelled, Clock::time_point::max() }, file);
    }
    started.acquire(2); // One batch can occupy both workers.
    const bool overflow = scheduler.submitFile({ cancelled, Clock::now() + std::chrono::milliseconds(20) }, [] {});
    std::atomic_bool single = false;
    scheduler.submit(1, [&] { single = true; });
    const bool bounded = !single.load();
    resume.release(2);
    scheduler.joinFiles();
    scheduler.shutdown();
    return !overflow && bounded && single.load();
}

bool checkCancelledChildren()
{
    treemcp::Scheduler scheduler(2);
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
    scheduler.submitFile(stop, [&, state = std::move(runningState)] {
        started.release();
        resumeFile.acquire();
    });
    started.acquire(2);
    scheduler.submitFile(stop, [&, state = std::move(queuedState)] { queuedRan = true; });
    cancelled = true;
    scheduler.cancelFiles();
    const bool removed = queued.expired() && !running.expired();
    resumeFile.release();
    scheduler.joinFiles();
    const bool released = running.expired() && !queuedRan;
    resumeSingle.release();
    scheduler.shutdown();
    if (removed && released) {
        std::puts("Cancelled queued child released immediately; running child's state released before join returned");
    }
    return removed && released;
}

} // namespace

int main()
{
    return checkFairness() && checkCapacity() && checkCancelledChildren() ? 0 : 1;
}
