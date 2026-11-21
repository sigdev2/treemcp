#ifndef TREEMCP_SESSION_HPP
#define TREEMCP_SESSION_HPP

#include "JsonRpc.hpp"
#include "Scheduler.hpp"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <atomic>
#include <functional>
#include <thread>

#include <optional>

namespace treemcp {

struct Workspace;

/** @brief Owns the handshake and request ID history of one stdio connection on the main thread. */
class Session : public QObject {
public:
    using ReplyWriter = std::function<void(const QJsonObject&)>;
    explicit Session(const Workspace& searchWorkspace, int workers = 1, ReplyWriter writer = {});
    ~Session() override;
    void shutdown();
    std::optional<QJsonObject> handle(QByteArrayView frame);

private:
    enum class Phase {
        AwaitInitialize,
        AwaitInitialized,
        Ready
    };

    using Clock = std::chrono::steady_clock;
    struct Work {
        std::atomic_bool cancelled = false;
        Clock::time_point deadline;
        int cost;
        bool batch;
        std::optional<QJsonObject> reply; // Guarded by completionMutex until workers join.
    };
    std::optional<QJsonObject>
        dispatch(const JsonRpc::Request& request, Clock::time_point received, qsizetype frameBytes);
    std::optional<QJsonObject> schedule(
        const QJsonValue& id,
        const QString& name,
        const QJsonObject& args,
        Clock::time_point received,
        qsizetype frameBytes);
    void cancel(const QJsonValue& id);
    void complete(const QJsonValue& id);
    void expire();
    void forget(const QJsonValue& id);

    QJsonObject initialize(const QJsonValue& id, const QJsonObject& params);

    const Workspace& workspace;
    const int maxRequests;
    ReplyWriter publish;
    Scheduler scheduler;
    bool closed = false;
    std::thread coordinator;
    QHash<QJsonValue, std::shared_ptr<Work>> active;
    std::mutex completionMutex;
    QTimer deadlines;
    int admittedCost = 0;
    Phase phase = Phase::AwaitInitialize;
    QSet<QJsonValue> usedIds;
};

} // namespace treemcp

#endif // TREEMCP_SESSION_HPP
