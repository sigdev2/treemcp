#ifndef TREEMCP_WORKSPACE_HPP
#define TREEMCP_WORKSPACE_HPP

#include "WorkspaceNode.hpp"
#include <QStringList>
#include <variant>

namespace treemcp {

struct BinaryFile {};
using SourceRead = std::variant<QByteArray, QString, BinaryFile>;
SourceRead readText(const WorkspaceNode& parent, const QString& name, const WorkspaceNode& opened, qsizetype limit);
SourceRead readWorkspaceFile(const WorkspaceNode& root, const QString& path);
struct FileScope {
    QStringList paths;
    QStringList include;
    QStringList exclude;
    QStringList extensions;
    bool singleFile = false;
};

using SourceVisitor = std::function<bool(const QString&, const QByteArray&)>;
using ScanDiagnostic = std::function<void(const QString&, const QString&)>;
QString scanWorkspace(
    const WorkspaceNode& root,
    const FileScope& scope,
    const SourceVisitor& visit,
    const ScanDiagnostic& report);

inline constexpr qsizetype MAX_FILE_BYTES = 8 * 1024 * 1024;

} // namespace treemcp
#endif // TREEMCP_WORKSPACE_HPP
