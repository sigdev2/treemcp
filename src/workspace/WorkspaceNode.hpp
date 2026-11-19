#ifndef TREEMCP_WORKSPACENODE_HPP
#define TREEMCP_WORKSPACENODE_HPP

#include <QByteArray>
#include <QString>

#include <optional>

namespace treemcp {

enum class NodeKind {
    File,
    Directory,
    Missing,
    Excluded,
    Error
};

struct FileStamp {
    quint64 device;
    quint64 identity;
    qint64 size;
    qint64 modified;
    qint64 changed;
    qint64 modifiedFraction;
    qint64 changedFraction;
    bool operator==(const FileStamp&) const = default;
};

/** Owns one native handle. Child lookup always starts from the opened directory. */
class WorkspaceNode {
public:
    static WorkspaceNode openRoot(const QString& path);
    WorkspaceNode child(const QString& component) const;
    ~WorkspaceNode();
    WorkspaceNode(WorkspaceNode&& other) noexcept;
    WorkspaceNode(const WorkspaceNode&) = delete;
    WorkspaceNode& operator=(const WorkspaceNode&) = delete;

    NodeKind kind() const
    {
        return nodeKind;
    }
    std::optional<FileStamp> stamp() const;
    qsizetype read(char* buffer, qsizetype size) const;

private:
    explicit WorkspaceNode(NodeKind kind, qintptr handle = -1) :
            nodeKind(kind),
            native(handle)
    {
    }
    NodeKind nodeKind;
    qintptr native;
};

bool validComponent(const QString& component);
bool validSelector(const QString& path);

} // namespace treemcp

#endif // TREEMCP_WORKSPACENODE_HPP
