#ifndef TREEMCP_TOOLS_HPP
#define TREEMCP_TOOLS_HPP

#include <QJsonObject>

namespace treemcp {
class WorkspaceNode;
}

namespace treemcp::Tools {

QJsonObject list(const QJsonValue& id, const QJsonObject& params);
QJsonObject call(const QJsonValue& id, const QJsonObject& params, const WorkspaceNode& root);

} // namespace treemcp::Tools

#endif // TREEMCP_TOOLS_HPP
