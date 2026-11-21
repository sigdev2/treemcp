#include "Scheduler.hpp"

#include <algorithm>

namespace treemcp {

Scheduler::Scheduler(int workers) :
        capacity(static_cast<size_t>(workers))
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
        changed.notify_all();
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
    changed.notify_all();
    threads.clear();
}

void Scheduler::submit(const QJsonValue& id, Job job)
{
    {
        std::lock_guard lock(mutex);
        singles.emplace_back(id, std::move(job));
    }
    changed.notify_all();
}

bool Scheduler::remove(const QJsonValue& id)
{
    std::lock_guard lock(mutex);
    return std::erase_if(singles, [&](const auto& entry) { return entry.first == id; }) != 0;
}

bool Scheduler::submitFile(const SearchStop& stop, Job job)
{
    std::unique_lock lock(mutex);
    changed.wait_until(lock, stop.deadline, [&] { return outstandingFiles < capacity || stop.requested(); });
    if (stop.requested()) {
        return false;
    }
    ++outstandingFiles;
    files.push_back(std::move(job));
    lock.unlock();
    changed.notify_all();
    return true;
}

void Scheduler::cancelFiles()
{
    {
        std::lock_guard lock(mutex);
        outstandingFiles -= files.size();
        files.clear();
    }
    changed.notify_all();
}

void Scheduler::joinFiles()
{
    std::unique_lock lock(mutex);
    changed.wait(lock, [&] { return outstandingFiles == 0; });
}

void Scheduler::run()
{
    std::unique_lock lock(mutex);
    for (;;) {
        changed.wait(lock, [&] { return closing || !files.empty() || !singles.empty(); });
        if (files.empty() && singles.empty()) {
            return;
        }
        const bool file = !files.empty() && (preferFile || singles.empty());
        preferFile = !file;
        {
            Job job;
            if (file) {
                job = std::move(files.front());
                files.pop_front();
            } else {
                job = std::move(singles.front().second);
                singles.pop_front();
            }
            lock.unlock();
            job();
        }
        lock.lock();
        if (file) {
            --outstandingFiles;
            changed.notify_all();
        }
    }
}

} // namespace treemcp
