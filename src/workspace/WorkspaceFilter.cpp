#include "WorkspaceFilter.hpp"

#include "Workspace.hpp"

namespace treemcp {

std::optional<WorkspaceGlob> WorkspaceGlob::compile(QString pattern, qsizetype baseDepth)
{
    if (pattern.size() > 512 || pattern.contains('\\') || pattern.contains(QChar::Null)) {
        return std::nullopt;
    }
    WorkspaceGlob glob;
    glob.base = baseDepth;
    glob.directoriesOnly = pattern.endsWith('/');
    if (glob.directoriesOnly) {
        pattern.chop(1);
    }
    glob.basename = !pattern.contains('/');
    if (pattern.startsWith('/')) {
        pattern.remove(0, 1);
    }
    // A trailing /** selects descendants and leaves the parent eligible for traversal.
    if (pattern.endsWith("/**")) {
        pattern += QStringLiteral("/*");
    }
    for (const QString& part : pattern.split('/')) {
        if (part.isEmpty() || part == "." || part == "..") {
            return std::nullopt;
        }
        if (part == "**") {
            glob.components.append(QRegularExpression {});
        } else {
            QRegularExpression expression(QRegularExpression::wildcardToRegularExpression(part));
            if (!expression.isValid()) {
                return std::nullopt;
            }
            glob.components.append(expression);
        }
    }
    return glob;
}

bool WorkspaceGlob::matches(const QStringList& parts, bool directory) const
{
    if (directoriesOnly && !directory) {
        return false;
    }
    qsizetype path = basename ? parts.size() - 1 : base;
    qsizetype pattern = 0;
    qsizetype star = -1;
    qsizetype consumed = path;
    while (path < parts.size()) {
        if (pattern < components.size() && components.at(pattern).pattern().isEmpty()) {
            star = pattern++;
            consumed = path;
        } else if (pattern < components.size() && components.at(pattern).match(parts.at(path)).hasMatch()) {
            ++path;
            ++pattern;
        } else if (star >= 0) {
            pattern = star + 1;
            path = ++consumed;
        } else {
            return false;
        }
    }
    while (pattern < components.size() && components.at(pattern).pattern().isEmpty()) {
        ++pattern;
    }
    return pattern == components.size();
}

QString IgnoreRules::extend(const WorkspaceNode& directory, qsizetype depth, const std::function<bool()>& stop)
{
    for (const auto& name : { QStringLiteral(".gitignore"), QStringLiteral(".ignore") }) {
        if (stop && stop()) {
            break;
        }
        const QString error = readFile(directory, name, depth, stop);
        if (!error.isEmpty()) {
            return error;
        }
    }
    return {};
}

QString IgnoreRules::readFile(
    const WorkspaceNode& directory, const QString& name, qsizetype depth, const std::function<bool()>& stop)
{
    const auto file = directory.child(name);
    if (file.kind() == NodeKind::Missing) {
        return {};
    }
    if (file.kind() != NodeKind::File) {
        return QStringLiteral("Cannot open regular %1").arg(name);
    }
    const auto read = readText(directory, name, file, 64 * 1024, stop);
    const auto* bytes = std::get_if<QByteArray>(&read);
    if (!bytes) {
        return QStringLiteral("Cannot read bounded UTF-8 %1").arg(name);
    }
    for (QString line : QString::fromUtf8(*bytes).split('\n')) {
        if (stop && stop()) {
            return {};
        }
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        while (line.endsWith(' ')) {
            line.chop(1);
        }
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        const bool exclude = !line.startsWith('!');
        if (!exclude) {
            line.remove(0, 1);
        }
        auto glob = WorkspaceGlob::compile(line, depth);
        if (!glob || rules.size() >= 256) {
            return QStringLiteral("Invalid %1 pattern or active rule limit exceeded").arg(name);
        }
        rules.append({ std::move(*glob), exclude });
    }
    return {};
}

bool IgnoreRules::excludes(const QStringList& parts, bool directory) const
{
    for (auto it = rules.crbegin(); it != rules.crend(); ++it) {
        if (it->glob.matches(parts, directory)) {
            return it->exclude;
        }
    }
    return false;
}

} // namespace treemcp
