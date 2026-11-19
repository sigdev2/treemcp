#ifndef TREEMCP_WORKSPACE_HPP
#define TREEMCP_WORKSPACE_HPP

#include "WorkspaceNode.hpp"
#include <variant>

namespace treemcp {

using SourceRead = std::variant<QByteArray, QString>;
SourceRead readText(const WorkspaceNode& parent, const QString& name, const WorkspaceNode& opened, qsizetype limit);
SourceRead readWorkspaceFile(const WorkspaceNode& root, const QString& path);
inline constexpr qsizetype MAX_FILE_BYTES = 8 * 1024 * 1024;

} // namespace treemcp
#endif // TREEMCP_WORKSPACE_HPP
