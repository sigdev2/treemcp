#include "WorkspaceFilter.hpp"

#include "Workspace.hpp"

namespace treemcp {

std::optional<WorkspaceGlob> WorkspaceGlob::compile(QString pattern)
{
    if (pattern.size() > 512 || pattern.contains('\\') || pattern.contains(QChar::Null)) {
        return std::nullopt;
    }
    WorkspaceGlob glob;
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
    qsizetype path = basename ? parts.size() - 1 : 0;
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

QString IgnoreRules::extend(const WorkspaceNode& directory, qsizetype baseOffset, const std::function<bool()>& stop)
{
    for (const auto& name : { QStringLiteral(".gitignore"), QStringLiteral(".ignore") }) {
        if (stop && stop()) {
            break;
        }
        const QString error = readFile(directory, name, baseOffset, stop);
        if (!error.isEmpty()) {
            return error;
        }
    }
    return {};
}

QString IgnoreRules::readFile(
    const WorkspaceNode& directory,
    const QString& name,
    qsizetype baseOffset,
    const std::function<bool()>& stop)
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
    QByteArrayView remaining(*bytes);
    if (remaining.startsWith("\xef\xbb\xbf")) {
        remaining = remaining.sliced(3);
    }
    while (!remaining.empty()) {
        if (stop && stop()) {
            return {};
        }
        const auto lf = remaining.indexOf('\n');
        auto line = lf < 0 ? remaining : remaining.first(lf);
        remaining = remaining.sliced(lf < 0 ? remaining.size() : lf + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (line.startsWith('#')) {
            continue;
        }
        // A quoted space is significant even if more unquoted spaces follow.
        qsizetype length = 0;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line[i] != ' ') {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    ++i;
                }
                length = i + 1;
            }
        }
        line = line.first(length);
        if (line.empty()) {
            continue;
        }
        if (line.size() > 512) {
            return QStringLiteral("%1 pattern exceeds 512 bytes").arg(name);
        }
        if (rules.size() >= 256) {
            return QStringLiteral("Active ignore rule limit exceeded in %1").arg(name);
        }
        const bool exclude = !line.startsWith('!');
        if (!exclude) {
            line = line.sliced(1);
        }
        rules.append({ IgnorePattern(line), baseOffset, exclude });
    }
    return {};
}

bool IgnoreRules::excludes(QByteArrayView path, bool directory, const std::function<bool()>& stop) const
{
    const auto basename = path.sliced(path.lastIndexOf('/') + 1);
    for (auto it = rules.crbegin(); it != rules.crend(); ++it) {
        if (stop && stop()) {
            return false;
        }
        if (it->pattern.matches(path.sliced(it->baseOffset), basename, directory, stop)) {
            return it->exclude;
        }
    }
    return false;
}

} // namespace treemcp
