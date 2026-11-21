#include "Workspace.hpp"
#include "WorkspaceFilter.hpp"

#include <map>
#include <QSet>

namespace treemcp {
namespace {

struct Selection {
    std::map<QString, Selection> children;
    bool all = false;
};

bool matches(const QList<WorkspaceGlob>& globs, const QStringList& parts, bool directory)
{
    return std::any_of(globs.begin(), globs.end(), [&](const WorkspaceGlob& glob) {
        return glob.matches(parts, directory);
    });
}

bool compileGlobs(const QStringList& patterns, QList<WorkspaceGlob>& output)
{
    for (const auto& pattern : patterns) {
        auto glob = WorkspaceGlob::compile(pattern);
        if (!glob) {
            return false;
        }
        output.append(std::move(*glob));
    }
    return true;
}

class Scan {
public:
    Scan(
        const Workspace& searchWorkspace,
        const FileScope& selection,
        const FileVisitor& onSource,
        const ScanDiagnostic& onError,
        const ScanOptions& limits) :
            workspace(searchWorkspace),
            scope(selection),
            visit(onSource),
            report(onError),
            options(limits)
    {
    }

    QString run()
    {
        Selection selection;
        for (const auto& path : scope.paths) {
            if (!validSelector(path) || (scope.singleFile && path == ".")) {
                return QStringLiteral("Expected a relative workspace path");
            }
            Selection* current = &selection;
            if (path != ".") {
                for (const auto& part : path.split('/')) {
                    current = &current->children[part];
                }
            }
            current->all = true;
        }
        if (!compileGlobs(scope.include, include) || !compileGlobs(scope.exclude, exclude)) {
            return QStringLiteral("Invalid workspace glob");
        }
        if (!options.stop || !options.stop()) {
            walk(workspace.root, {}, selection, {});
        }
        return {};
    }

private:
    const Workspace& workspace;
    const FileScope& scope;
    const FileVisitor& visit;
    const ScanDiagnostic& report;
    const ScanOptions& options;
    QList<WorkspaceGlob> include;
    QList<WorkspaceGlob> exclude;
    int entries = 0;
    int files = 0;
    bool stopped = false;

    void walk(
        const std::shared_ptr<const WorkspaceNode>& directory,
        const QStringList& prefix,
        const Selection& selection,
        IgnoreRules rules)
    {
        const QString directoryPath = prefix.isEmpty() ? QStringLiteral(".") : prefix.join('/');
        const QString error = rules.extend(*directory, prefix.size(), options.stop);
        if (!error.isEmpty()) {
            report(directoryPath, error);
            return;
        }
        QSet<QString> found;
        const bool listed = directory->list([&](const QString& name, NodeKind kind) {
            if (options.stop && options.stop()) {
                stopped = true;
                return false;
            }
            if (++entries > 100000) {
                report(directoryPath, QStringLiteral("Directory entry limit exceeded"));
                stopped = true;
                return false;
            }
            const auto explicitChild = selection.children.find(name);
            if (!selection.all && explicitChild == selection.children.end()) {
                return true;
            }
            if (explicitChild != selection.children.end()) {
                found.insert(name);
            }
            if (!name.startsWith('.') && validComponent(name)) {
                static const Selection ALL { {}, true };
                const Selection& child = selection.all ? ALL : explicitChild->second;
                QStringList parts = prefix;
                parts.append(name);
                process(directory, parts, child, rules, kind);
            }
            return !stopped && (selection.all || static_cast<size_t>(found.size()) < selection.children.size());
        });
        if (!listed) {
            report(directoryPath, QStringLiteral("Cannot enumerate workspace directory"));
        } else if (!stopped && !selection.all) {
            for (const auto& [name, child] : selection.children) {
                if (!found.contains(name)) {
                    report(
                        prefix.isEmpty() ? name : directoryPath + '/' + name,
                        QStringLiteral("Workspace path not found"));
                }
            }
        }
    }

    void process(
        const std::shared_ptr<const WorkspaceNode>& parent,
        const QStringList& parts,
        const Selection& selection,
        const IgnoreRules& rules,
        NodeKind kind)
    {
        const QString& name = parts.back();
        const bool ignoreDirectory = !scope.singleFile && workspace.ignoredDirectories.contains(name);
        if (kind == NodeKind::Directory && ignoreDirectory) {
            return;
        }
        const QString path = parts.join('/');
        if (parts.size() > 64 || path.toUtf8().size() > MAX_PATH_BYTES) {
            report(path, QStringLiteral("Workspace path limit exceeded"));
            return;
        }
        const auto excluded = [&](bool dir) { return matches(exclude, parts, dir) || rules.excludes(parts, dir); };
        const bool excludeFile = excluded(false);
        const bool excludeDirectory = ignoreDirectory || excluded(true);
        if (excludeDirectory && (kind == NodeKind::Directory || excludeFile)) {
            return;
        }
        const auto eligibleFile = [&] {
            return !excludeFile && (include.isEmpty() || matches(include, parts, false)) &&
                (scope.extensions.isEmpty() ||
                 std::any_of(scope.extensions.begin(), scope.extensions.end(), [&](const QString& suffix) {
                     return name.endsWith(suffix);
                 }));
        };
        if (kind == NodeKind::File && selection.all && !eligibleFile()) {
            return;
        }
        auto node = parent->child(name);
        if (node.kind() == NodeKind::Directory) {
            if (!excludeDirectory && !(scope.singleFile && selection.all)) {
                walk(std::make_shared<WorkspaceNode>(std::move(node)), parts, selection, rules);
            }
            return;
        }
        if (node.kind() == NodeKind::Excluded) {
            return;
        }
        if (node.kind() != NodeKind::File || !selection.all) {
            report(path, QStringLiteral("Cannot open selected workspace entry"));
            return;
        }
        if (kind != NodeKind::File && !eligibleFile()) {
            return;
        }
        if (files == options.maxFiles) {
            report(path, QStringLiteral("Workspace file limit exceeded"));
            stopped = true;
            return;
        }
        ++files;
        if (options.visitedFiles) {
            *options.visitedFiles = files;
        }
        stopped = !visit(path, { parent, name, std::move(node) });
    }
};

} // namespace

QString scanWorkspace(
    const Workspace& workspace,
    const FileScope& scope,
    const FileVisitor& visit,
    const ScanDiagnostic& report,
    const ScanOptions& options)
{
    return Scan(workspace, scope, visit, report, options).run();
}

SourceRead SelectedFile::read(const std::function<bool()>& stop) const
{
    return readText(*parent, name, node, MAX_FILE_BYTES, stop);
}

SourceRead readWorkspaceFile(const Workspace& workspace, const QString& path, const std::function<bool()>& stop)
{
    SourceRead result = QStringLiteral("Expected an eligible regular UTF-8 workspace file");
    const FileScope scope { { path }, {}, {}, {}, true };
    const QString error = scanWorkspace(
        workspace,
        scope,
        [&](const QString&, SelectedFile file) {
            auto read = file.read(stop);
            if (!std::holds_alternative<BinaryFile>(read)) {
                result = std::move(read);
            }
            return false;
        },
        [&](const QString&, const QString& diagnostic) { result = diagnostic; },
        { 10000, stop });
    if (!error.isEmpty()) {
        return error;
    }
    return result;
}

} // namespace treemcp
