#include "Scheduler.hpp"
#include "SearchProfile.hpp"

#include <algorithm>
#include <QScopeGuard>

namespace treemcp {

Scheduler::Scheduler(int workers) :
        capacity(static_cast<size_t>(workers)),
        fileCapacity(capacity)
{
    try {
        for (int i = 0; i < workers; ++i) {
            threads.emplace_back([this] { run(); });
        }
    } catch (...) {
        {
            std::lock_guard lock(mutex);
            closing = true;
        }
        ready.notify_all();
        throw;
    }
}

Scheduler::~Scheduler()
{
    shutdown();
}

void Scheduler::shutdown()
{
    {
        std::lock_guard lock(mutex);
        closing = true;
    }
    ready.notify_all();
    changed.notify_all();
    threads.clear();
}

void Scheduler::submit(const QJsonValue& id, Job job)
{
    {
        std::lock_guard lock(mutex);
        singles.emplace_back(id, std::move(job));
    }
    ready.notify_one();
}

bool Scheduler::remove(const QJsonValue& id)
{
    std::lock_guard lock(mutex);
    return std::erase_if(singles, [&](const auto& entry) { return entry.first == id; }) != 0;
}

bool Scheduler::submitFile(FileJobs& owner, const SearchStop& stop, Job job, bool waitForCapacity)
{
    std::unique_lock lock(mutex);
    const auto interrupted = [&] { return closing || owner.cancelled || stop.requested(); };
    if (!interrupted() && (outstandingFiles >= fileCapacity || !fileSubmitters.empty())) {
        if (!waitForCapacity) {
            return false;
        }
        const SearchProfile::Timer wait(SearchProfile::Phase::Queue);
        fileSubmitters.push_back(&owner);
        changed.wait_until(lock, stop.deadline, [&] {
            return interrupted() || (fileSubmitters.front() == &owner && outstandingFiles < fileCapacity);
        });
        std::erase(fileSubmitters, &owner);
    }
    if (interrupted()) {
        changed.notify_all();
        return false;
    }
    files.emplace_back(&owner, SearchProfile::fileJob(std::move(job)));
    ++owner.outstanding;
    ++outstandingFiles;
    const bool moreCapacity = !fileSubmitters.empty() && outstandingFiles < fileCapacity;
    lock.unlock();
    ready.notify_one();
    if (moreCapacity) {
        changed.notify_all();
    }
    return true;
}

bool Scheduler::tryRunFileInline(FileJobs& owner, const SearchStop& stop, const Job& job)
{
    {
        std::lock_guard lock(mutex);
        if (closing || owner.cancelled || stop.requested() || runningJobs == capacity ||
            outstandingFiles >= fileCapacity || !singles.empty() || !files.empty() || !fileSubmitters.empty())
        {
            return false;
        }
        ++runningJobs;
        ++runningFiles;
        ++outstandingFiles;
        ++owner.outstanding;
    }
    const auto release = qScopeGuard([&] {
        std::lock_guard lock(mutex);
        --runningJobs;
        --runningFiles;
        --outstandingFiles;
        --owner.outstanding;
        changed.notify_all();
        if (!singles.empty() || !files.empty()) {
            ready.notify_one();
        }
    });
    const SearchProfile::Timer busy(SearchProfile::Phase::Busy);
    job();
    return true;
}

void Scheduler::cancelFiles(FileJobs& owner)
{
    {
        std::lock_guard lock(mutex);
        owner.cancelled = true;
        const auto removed = std::erase_if(files, [&](const auto& entry) { return entry.first == &owner; });
        owner.outstanding -= removed;
        outstandingFiles -= removed;
    }
    changed.notify_all();
}

void Scheduler::joinFiles(const FileJobs& owner)
{
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return owner.outstanding == 0; });
}

void Scheduler::reserveWorkerForSingles(bool reserve)
{
    {
        std::lock_guard lock(mutex);
        const size_t limit = reserve && capacity > 1 ? capacity - 1 : capacity;
        if (fileCapacity == limit) {
            return;
        }
        fileCapacity = limit;
    }
    ready.notify_one();
    changed.notify_all();
}

void Scheduler::run()
{
    std::unique_lock lock(mutex);
    const auto canRunFile = [&] { return !files.empty() && (closing || runningFiles < fileCapacity); };
    for (;;) {
        ready.wait(lock, [&] {
            return (closing && files.empty() && singles.empty()) ||
                (runningJobs < capacity && (canRunFile() || !singles.empty()));
        });
        if (files.empty() && singles.empty()) {
            return;
        }
        const bool file = canRunFile() && (preferFile || singles.empty());
        preferFile = !file;
        FileJobs* owner = nullptr;
        {
            Job job;
            if (file) {
                owner = files.front().first;
                job = std::move(files.front().second);
                files.pop_front();
                ++runningFiles;
            } else {
                job = std::move(singles.front().second);
                singles.pop_front();
            }
            if (closing && singles.empty() && files.empty()) {
                ready.notify_all();
            }
            ++runningJobs;
            lock.unlock();
            job();
        }
        lock.lock();
        --runningJobs;
        if (owner) {
            --owner->outstanding;
            --outstandingFiles;
            --runningFiles;
            changed.notify_all();
            if (canRunFile()) {
                ready.notify_one();
            }
        }
    }
}

} // namespace treemcp
