#include "SearchProfile.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <mutex>

namespace treemcp::SearchProfile {
namespace {

thread_local Request* current = nullptr;
constexpr std::array NAMES { "scan",    "list",  "listVisitor", "ignoreRead", "filter",      "open",  "queue",
                             "read",    "parse", "query",       "collect",    "collectLock", "merge", "serialize",
                             "compile", "busy",  "selected",    "committed",  "resultWait" };
static_assert(NAMES.size() == static_cast<size_t>(Phase::Count));

QString directory()
{
    return qEnvironmentVariable("TREEMCP_SEARCH_PROFILE_DIR");
}

qint64 nanoseconds(Clock::duration duration)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

void record(const QJsonObject& entry)
{
    const auto path = directory();
    if (path.isEmpty()) {
        return;
    }
    static std::mutex mutex;
    const auto filename = QString::number(QCoreApplication::applicationPid()) + QStringLiteral(".jsonl");
    std::lock_guard lock(mutex);
    QFile file(QDir(path).filePath(filename));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
    }
}

} // namespace

Request::Request(const QJsonValue& requestId, const QString& name) :
        id(requestId),
        tool(name),
        started(Clock::now())
{
    if (!directory().isEmpty()) {
        current = this;
    }
}

Request::~Request()
{
    if (current != this) {
        return;
    }
    const auto finished = Clock::now();
    current = nullptr;
    QJsonObject stats;
    for (size_t i = 0; i < NAMES.size(); ++i) {
        const auto& counters = phases[i];
        stats.insert(
            QLatin1String(NAMES[i]),
            QJsonObject { { "calls", qint64 { counters.calls.load() } }, { "ns", qint64 { counters.ns.load() } } });
    }
    record({ { "id", id },
             { "event", "finished" },
             { "tool", tool },
             { "phases", stats },
             { "startNs", nanoseconds(started.time_since_epoch()) },
             { "timeNs", nanoseconds(finished.time_since_epoch()) } });
}

Timer::Timer(Phase phase, bool enabled) :
        counters(current && enabled ? &current->phases[static_cast<size_t>(phase)] : nullptr),
        started(counters ? Clock::now() : Clock::time_point {})
{
}

void Timer::finish()
{
    if (counters) {
        counters->calls.fetch_add(1, std::memory_order_relaxed);
        counters->ns.fetch_add(nanoseconds(Clock::now() - started), std::memory_order_relaxed);
        counters = nullptr;
    }
}

void count(Phase phase)
{
    if (current) {
        current->phases[static_cast<size_t>(phase)].calls.fetch_add(1, std::memory_order_relaxed);
    }
}

std::function<void()> fileJob(std::function<void()> job)
{
    if (!current) {
        return job;
    }
    return [profile = current, job = std::move(job)] {
        current = profile;
        {
            const Timer busy(Phase::Busy);
            job();
        }
        current = nullptr;
    };
}

void event(const QJsonValue& id, const char* type, Clock::time_point time)
{
    record(
        { { "id", id }, { "event", QString::fromLatin1(type) }, { "timeNs", nanoseconds(time.time_since_epoch()) } });
}

void wireSerialized(const QJsonValue& id, Clock::duration duration, qsizetype bytes)
{
    record({ { "id", id }, { "event", "wireSerialize" }, { "ns", nanoseconds(duration) }, { "bytes", bytes } });
}

} // namespace treemcp::SearchProfile
