#include "WorkspaceNode.hpp"

#include <QStringList>
#include <utility>

namespace treemcp {

WorkspaceNode::WorkspaceNode(WorkspaceNode&& other) noexcept :
        nodeKind(std::exchange(other.nodeKind, NodeKind::Error)),
        native(std::exchange(other.native, -1))
{
}

bool validComponent(const QString& component)
{
    if (component.isEmpty() || component.size() > 255 || component.endsWith('.') || component.endsWith(' ') ||
        !component.isValidUtf16())
    {
        return false;
    }
    for (const QChar ch : component) {
        if (ch.unicode() < 32 || QStringLiteral("/\\:\"*?<>|").contains(ch)) {
            return false;
        }
    }
    const QString stem = component.section('.', 0, 0).trimmed().toUpper();
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" || stem == "CONOUT$") {
        return false;
    }
    return !(
        stem.size() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")) &&
        QStringLiteral("0123456789\u00b9\u00b2\u00b3").contains(stem.back()));
}

bool validSelector(const QString& path)
{
    if (path == ".") {
        return true;
    }
    if (path.size() > 4096 || path.count('/') >= 64) {
        return false;
    }
    const QStringList parts = path.split('/');
    return std::all_of(parts.begin(), parts.end(), validComponent);
}

} // namespace treemcp
