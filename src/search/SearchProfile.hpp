#ifndef TREEMCP_SEARCHPROFILE_HPP
#define TREEMCP_SEARCHPROFILE_HPP

#include <QJsonDocument>
#include <QJsonValue>

#include <chrono>
#include <functional>

#ifdef TREEMCP_SEARCH_PROFILE
#include <array>
#include <atomic>
#endif

namespace treemcp::SearchProfile {

using Clock = std::chrono::steady_clock;
enum class Phase {
    Scan,
    List,
    ListVisitor,
    IgnoreRead,
    Filter,
    Open,
    Queue,
    Read,
    Parse,
    Query,
    Collect,
    CollectLock,
    Merge,
    Serialize,
    Compile,
    Busy,
    Selected,
    Committed,
    ResultWait,
    Count
};

#ifdef TREEMCP_SEARCH_PROFILE
struct Counters {
    std::atomic_int64_t calls = 0;
    std::atomic_int64_t ns = 0;
};

struct Request {
    Request(const QJsonValue& id, const QString& tool);
    ~Request();

    std::array<Counters, static_cast<size_t>(Phase::Count)> phases;
    QJsonValue id;
    QString tool;
    Clock::time_point started;
};

class Timer {
public:
    explicit Timer(Phase phase, bool enabled = true);
    ~Timer()
    {
        finish();
    }
    void finish();

private:
    Counters* counters;
    Clock::time_point started;
};

void count(Phase phase);
std::function<void()> fileJob(std::function<void()> job);
void event(const QJsonValue& id, const char* type, Clock::time_point time);
void wireSerialized(const QJsonValue& id, Clock::duration duration, qsizetype bytes);
#else
struct Request {
    Request(const QJsonValue&, const QString&)
    {
    }
};

struct Timer {
    explicit Timer(Phase, bool = true)
    {
    }
    void finish()
    {
    }
};

inline void count(Phase)
{
}
inline std::function<void()> fileJob(std::function<void()> job)
{
    return job;
}
#endif

inline QByteArray compactJson(const QJsonDocument& document)
{
    const Timer serialize(Phase::Serialize);
    return document.toJson(QJsonDocument::Compact);
}

} // namespace treemcp::SearchProfile
#endif // TREEMCP_SEARCHPROFILE_HPP
