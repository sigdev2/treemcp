#ifndef TREEMCP_SCHEDULER_HPP
#define TREEMCP_SCHEDULER_HPP

#include "SearchStop.hpp"

#include <QJsonValue>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace treemcp {

/** File jobs of one request, submitted by one coordinator. Outlives the jobs and their join. */
class FileJobs {
public:
    FileJobs() = default;
    FileJobs(const FileJobs&) = delete;
    FileJobs& operator=(const FileJobs&) = delete;

private:
    friend class Scheduler;
    size_t outstanding = 0;
    bool cancelled = false;
};

/** Session-owned pool. Coordinators feed file jobs from outside the pool. */
class Scheduler {
public:
    using Job = std::function<void()>;
    explicit Scheduler(int workers);
    ~Scheduler();
    void submit(const QJsonValue& id, Job job);
    bool remove(const QJsonValue& id);
    size_t workerCount() const
    {
        return capacity;
    }
    bool submitFile(FileJobs& owner, const SearchStop& stop, Job job, bool waitForCapacity = true);
    bool tryRunFileInline(FileJobs& owner, const SearchStop& stop, const Job& job);
    void cancelFiles(FileJobs& owner);
    void joinFiles(const FileJobs& owner);
    void reserveWorkerForSingles(bool reserve);
    void shutdown();

private:
    void run();
    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable changed;
    std::deque<std::pair<QJsonValue, Job>> singles;
    std::deque<std::pair<FileJobs*, Job>> files;
    std::deque<FileJobs*> fileSubmitters;
    const size_t capacity;
    size_t fileCapacity;
    size_t outstandingFiles = 0;
    size_t runningFiles = 0;
    size_t runningJobs = 0;
    bool preferFile = false;
    bool closing = false;
    std::vector<std::jthread> threads;
};

} // namespace treemcp
#endif // TREEMCP_SCHEDULER_HPP
