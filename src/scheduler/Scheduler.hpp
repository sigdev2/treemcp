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

/** Session-owned pool. The main thread admits singles and one coordinator feeds batch files. */
class Scheduler {
public:
    using Job = std::function<void()>;
    explicit Scheduler(int workers);
    ~Scheduler();
    void submit(const QJsonValue& id, Job job);
    bool remove(const QJsonValue& id);
    bool submitFile(const SearchStop& stop, Job job);
    void cancelFiles();
    void joinFiles();
    void shutdown();

private:
    void run();
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::pair<QJsonValue, Job>> singles;
    std::deque<Job> files;
    const size_t capacity;
    size_t outstandingFiles = 0;
    bool preferFile = false;
    bool closing = false;
    std::vector<std::jthread> threads;
};

} // namespace treemcp
#endif // TREEMCP_SCHEDULER_HPP
