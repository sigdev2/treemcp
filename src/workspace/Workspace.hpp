#ifndef TREEMCP_WORKSPACE_HPP
#define TREEMCP_WORKSPACE_HPP

#include "WorkspaceNode.hpp"
#include <memory>
#include <QSet>
#include <QStringList>
#include <variant>

namespace treemcp {

struct Workspace {
    Workspace(WorkspaceNode directory, QSet<QString> exclusions) :
            root(std::make_shared<WorkspaceNode>(std::move(directory))),
            ignoredDirectories(std::move(exclusions))
    {
    }
    std::shared_ptr<const WorkspaceNode> root;
    QSet<QString> ignoredDirectories;
};

struct BinaryFile {};
using SourceRead = std::variant<QByteArray, QString, BinaryFile>;
SourceRead readText(
    const WorkspaceNode& parent,
    const QString& name,
    const WorkspaceNode& opened,
    qsizetype limit,
    const std::function<bool()>& stop = {});
SourceRead readWorkspaceFile(const Workspace& workspace, const QString& path, const std::function<bool()>& stop);
struct FileScope {
    QStringList paths;
    QStringList include;
    QStringList exclude;
    QStringList extensions;
    bool singleFile = false;
};

struct ScanOptions {
    int maxFiles = 10000;
    std::function<bool()> stop;
    int* visitedFiles = nullptr;
};

struct SelectedFile {
    std::shared_ptr<const WorkspaceNode> parent;
    QString name;
    WorkspaceNode node;
    SourceRead read(const std::function<bool()>& stop) const;
};

using FileVisitor = std::function<bool(const QString&, SelectedFile)>;
using ScanDiagnostic = std::function<void(const QString&, const QString&)>;
QString scanWorkspace(
    const Workspace& workspace,
    const FileScope& scope,
    const FileVisitor& visit,
    const ScanDiagnostic& report,
    const ScanOptions& options = {});

inline constexpr qsizetype MAX_FILE_BYTES = 8 * 1024 * 1024;

} // namespace treemcp
#endif // TREEMCP_WORKSPACE_HPP
