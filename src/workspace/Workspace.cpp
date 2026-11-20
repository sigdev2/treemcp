#include "Workspace.hpp"

#include <array>
#include <QStringList>

namespace treemcp {
namespace {

std::optional<SourceRead> readOnce(const WorkspaceNode& file, qsizetype limit, const std::function<bool()>& stop)
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
        if (stop && stop()) {
            return QStringLiteral("Workspace read interrupted");
        }
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
    const bool binary = std::any_of(bytes.cbegin(), bytes.cend(), [](char ch) {
        const auto byte = static_cast<unsigned char>(ch);
        return byte < 32 && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f';
    });
    if (binary || !bytes.isValidUtf8()) {
        return BinaryFile {};
    }
    return bytes;
}

} // namespace

SourceRead readText(
    const WorkspaceNode& parent,
    const QString& name,
    const WorkspaceNode& opened,
    qsizetype limit,
    const std::function<bool()>& stop)
{
    if (auto result = readOnce(opened, limit, stop)) {
        return *result;
    }
    const auto replacement = parent.child(name);
    if (replacement.kind() == NodeKind::File) {
        if (auto result = readOnce(replacement, limit, stop)) {
            return *result;
        }
    }
    return QStringLiteral("Workspace file changed during read");
}

} // namespace treemcp
