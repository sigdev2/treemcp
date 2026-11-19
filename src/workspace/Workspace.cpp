#include "Workspace.hpp"

#include <array>
#include <QStringList>

namespace treemcp {
namespace {

std::optional<SourceRead> readOnce(const WorkspaceNode& file, qsizetype limit)
{
    const auto before = file.stamp();
    if (!before || before->size < 0) {
        return QStringLiteral("Cannot inspect workspace file");
    }
    if (before->size > limit) {
        return QStringLiteral("Workspace file exceeds byte limit");
    }
    QByteArray bytes;
    bytes.reserve(before->size);
    std::array<char, 65536> buffer;
    while (bytes.size() <= before->size) {
        const qsizetype count =
            file.read(buffer.data(), std::min<qsizetype>(buffer.size(), before->size - bytes.size() + 1));
        if (count < 0) {
            return QStringLiteral("Cannot read workspace file");
        }
        if (count == 0) {
            break;
        }
        bytes.append(buffer.data(), count);
    }
    if (file.stamp() != before || bytes.size() != before->size) {
        return std::nullopt;
    }
    if (bytes.contains('\0') || !bytes.isValidUtf8()) {
        return QStringLiteral("Workspace file is binary or invalid UTF-8");
    }
    return bytes;
}

SourceRead descend(const WorkspaceNode& parent, const QStringList& parts, qsizetype index)
{
    const QString& name = parts.at(index);
    if (name.startsWith('.')) {
        return QStringLiteral("Hidden workspace path");
    }
    const auto node = parent.child(name);
    if (index + 1 == parts.size() && node.kind() == NodeKind::File) {
        return readText(parent, name, node, MAX_FILE_BYTES);
    }
    if (index + 1 < parts.size() && node.kind() == NodeKind::Directory) {
        return descend(node, parts, index + 1);
    }
    return QStringLiteral("Cannot open regular workspace file");
}

} // namespace

SourceRead readText(const WorkspaceNode& parent, const QString& name, const WorkspaceNode& opened, qsizetype limit)
{
    if (auto result = readOnce(opened, limit)) {
        return *result;
    }
    const auto replacement = parent.child(name);
    if (replacement.kind() == NodeKind::File) {
        if (auto result = readOnce(replacement, limit)) {
            return *result;
        }
    }
    return QStringLiteral("Workspace file changed during read");
}

SourceRead readWorkspaceFile(const WorkspaceNode& root, const QString& path)
{
    if (path == "." || !validSelector(path)) {
        return QStringLiteral("Expected a relative workspace file path");
    }
    return descend(root, path.split('/'), 0);
}

} // namespace treemcp
