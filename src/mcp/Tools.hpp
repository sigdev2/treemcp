#ifndef TREEMCP_TOOLS_HPP
#define TREEMCP_TOOLS_HPP

#include <functional>
#include <optional>
#include <QJsonObject>

namespace treemcp {
struct Workspace;
struct BatchControl;
} // namespace treemcp

namespace treemcp::Tools {

QJsonObject dataReply(const QJsonValue& id, const QJsonObject& data);
QJsonObject list(const QJsonValue& id, const QJsonObject& params);
using BatchRunner = std::function<std::optional<QJsonObject>(const QJsonObject&)>;
QJsonObject batchReply(
    const QJsonValue& id,
    const QJsonObject& arguments,
    const Workspace& workspace,
    const BatchControl& control);
std::optional<QJsonObject>
    call(const QJsonValue& id, const QJsonObject& params, const Workspace& workspace, const BatchRunner& runBatch);

} // namespace treemcp::Tools

#endif // TREEMCP_TOOLS_HPP
