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
    Scan(const FileScope& selection, const SourceVisitor& onSource, const ScanDiagnostic& onError) :
            scope(selection),
            visit(onSource),
            report(onError)
    {
    }

    QString run(const WorkspaceNode& root)
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
        walk(root, {}, selection, {});
        return {};
    }

private:
    const FileScope& scope;
    const SourceVisitor& visit;
    const ScanDiagnostic& report;
    QList<WorkspaceGlob> include;
    QList<WorkspaceGlob> exclude;
    int entries = 0;
    int files = 0;
    bool stopped = false;

    void walk(const WorkspaceNode& directory, const QStringList& prefix, const Selection& selection, IgnoreRules rules)
    {
        const QString directoryPath = prefix.isEmpty() ? QStringLiteral(".") : prefix.join('/');
        const QString error = rules.extend(directory, prefix.size());
        if (!error.isEmpty()) {
            report(directoryPath, error);
            return;
        }
        QSet<QString> found;
        const bool listed = directory.list([&](const QString& name, bool regularFile) {
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
                process(directory, parts, child, rules, regularFile);
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
        const WorkspaceNode& parent,
        const QStringList& parts,
        const Selection& selection,
        const IgnoreRules& rules,
        bool regularFile)
    {
        const QString& name = parts.back();
        const QString path = parts.join('/');
        if (parts.size() > 64 || path.toUtf8().size() > MAX_PATH_BYTES) {
            report(path, QStringLiteral("Workspace path limit exceeded"));
            return;
        }
        const auto excluded = [&](bool dir) { return matches(exclude, parts, dir) || rules.excludes(parts, dir); };
        const bool excludeFile = excluded(false);
        const bool excludeDirectory = excluded(true);
        if (excludeFile && excludeDirectory) {
            return;
        }
        const auto eligibleFile = [&] {
            return !excludeFile && (include.isEmpty() || matches(include, parts, false)) &&
                (scope.extensions.isEmpty() ||
                 std::any_of(scope.extensions.begin(), scope.extensions.end(), [&](const QString& suffix) {
                     return name.endsWith(suffix);
                 }));
        };
        if (regularFile && selection.all && !eligibleFile()) {
            return;
        }
        const auto node = parent.child(name);
        if (node.kind() == NodeKind::Directory) {
            if (!excludeDirectory && !(scope.singleFile && selection.all)) {
                walk(node, parts, selection, rules);
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
        if (!regularFile && !eligibleFile()) {
            return;
        }
        if (++files > 10000) {
            report(path, QStringLiteral("Workspace file limit exceeded"));
            stopped = true;
            return;
        }
        const auto read = readText(parent, name, node, MAX_FILE_BYTES);
        if (const auto* bytes = std::get_if<QByteArray>(&read)) {
            stopped = !visit(path, *bytes);
        } else if (const auto* error = std::get_if<QString>(&read)) {
            report(path, *error);
        }
    }
};

} // namespace

QString scanWorkspace(
    const WorkspaceNode& root,
    const FileScope& scope,
    const SourceVisitor& visit,
    const ScanDiagnostic& report)
{
    return Scan(scope, visit, report).run(root);
}

SourceRead readWorkspaceFile(const WorkspaceNode& root, const QString& path)
{
    SourceRead result = QStringLiteral("Expected an eligible regular UTF-8 workspace file");
    const FileScope scope { { path }, {}, {}, {}, true };
    const QString error = scanWorkspace(
        root,
        scope,
        [&](const QString&, const QByteArray& bytes) {
            result = bytes;
            return false;
        },
        [&](const QString&, const QString& diagnostic) { result = diagnostic; });
    if (!error.isEmpty()) {
        return error;
    }
    return result;
}

} // namespace treemcp
