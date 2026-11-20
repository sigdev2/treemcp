#ifndef TREEMCP_SESSION_HPP
#define TREEMCP_SESSION_HPP

#include "JsonRpc.hpp"

#include <QObject>
#include <QSet>

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
    explicit Session(const Workspace& searchWorkspace, int workers = 1, ReplyWriter writer = {}) :
            workspace(searchWorkspace),
            workerCount(workers),
            publish(std::move(writer))
    {
    }
    ~Session() override;
    void stopBatch();
    std::optional<QJsonObject> handle(QByteArrayView frame);

private:
    enum class Phase {
        AwaitInitialize,
        AwaitInitialized,
        Ready
    };

    std::optional<QJsonObject> dispatch(const JsonRpc::Request& request);
    QJsonObject initialize(const QJsonValue& id, const QJsonObject& params);

    const Workspace& workspace;
    const int workerCount;
    ReplyWriter publish;
    QJsonValue batchId = QJsonValue::Undefined;
    std::atomic_bool cancelled = false;
    std::thread worker;
    Phase phase = Phase::AwaitInitialize;
    QSet<QJsonValue> usedIds;
};

} // namespace treemcp

#endif // TREEMCP_SESSION_HPP
