#ifndef TREEMCP_TOOLS_HPP
#define TREEMCP_TOOLS_HPP

#include <functional>
#include <optional>
#include <QJsonObject>

namespace treemcp {
struct Workspace;
struct SearchStop;
class Scheduler;
} // namespace treemcp

namespace treemcp::Tools {

QJsonObject dataReply(const QJsonValue& id, const QJsonObject& data);
QJsonObject deadlineReply(const QJsonValue& id, const QJsonObject& reply = {});
QJsonObject list(const QJsonValue& id, const QJsonObject& params);
using Runner = std::function<std::optional<QJsonObject>(const QString&, const QJsonObject&)>;
std::optional<QJsonObject> call(const QJsonValue& id, const QJsonObject& params, const Runner& run);
QJsonObject execute(
    const QJsonValue& id,
    const QString& name,
    const QJsonObject& arguments,
    const Workspace& workspace,
    const SearchStop& stop,
    Scheduler& scheduler);

} // namespace treemcp::Tools
#endif // TREEMCP_TOOLS_HPP
