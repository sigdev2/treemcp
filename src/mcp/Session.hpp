#ifndef TREEMCP_SESSION_HPP
#define TREEMCP_SESSION_HPP

#include "JsonRpc.hpp"

#include <QSet>

#include <optional>

namespace treemcp {

class WorkspaceNode;

/** @brief Owns the handshake and request ID history of one stdio connection on the main thread. */
class Session {
public:
    explicit Session(const WorkspaceNode& workspace) :
            root(workspace)
    {
    }
    std::optional<QJsonObject> handle(QByteArrayView frame);

private:
    enum class Phase {
        AwaitInitialize,
        AwaitInitialized,
        Ready
    };

    std::optional<QJsonObject> dispatch(const JsonRpc::Request& request);
    QJsonObject initialize(const QJsonValue& id, const QJsonObject& params);

    const WorkspaceNode& root;
    Phase phase = Phase::AwaitInitialize;
    QSet<QJsonValue> usedIds;
};

} // namespace treemcp

#endif // TREEMCP_SESSION_HPP
