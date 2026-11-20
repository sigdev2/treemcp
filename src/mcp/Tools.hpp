#ifndef TREEMCP_TOOLS_HPP
#define TREEMCP_TOOLS_HPP

#include <functional>
#include <optional>
#include <QJsonObject>

namespace treemcp {
class WorkspaceNode;
struct BatchControl;
} // namespace treemcp

namespace treemcp::Tools {

QJsonObject dataReply(const QJsonValue& id, const QJsonObject& data);
QJsonObject list(const QJsonValue& id, const QJsonObject& params);
using BatchRunner = std::function<std::optional<QJsonObject>(const QJsonObject&)>;
QJsonObject batchReply(
    const QJsonValue& id,
    const QJsonObject& arguments,
    const WorkspaceNode& root,
    const BatchControl& control);
std::optional<QJsonObject>
    call(const QJsonValue& id, const QJsonObject& params, const WorkspaceNode& root, const BatchRunner& runBatch);

} // namespace treemcp::Tools

#endif // TREEMCP_TOOLS_HPP
