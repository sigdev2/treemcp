#include "Workspace.hpp"

#include <cstdio>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

int main()
{
    QTemporaryDir temp;
    const QString name = QStringLiteral("selected.py");
    const QString path = temp.filePath(name);
    QFile original(path);
    if (!temp.isValid() || !original.open(QIODevice::WriteOnly) || original.write("original") != 8) {
        return 1;
    }
    original.close();
    const treemcp::Workspace workspace { treemcp::WorkspaceNode::openRoot(temp.path()), {} };
    bool replaced = false;
    const bool listed = workspace.root->list([&](const QString& entry, treemcp::NodeKind hint) {
        if (entry != name) {
            return true;
        }
        // Hold the real directory stream open while the listed regular file changes type.
        replaced = hint == treemcp::NodeKind::File && QFile::rename(path, temp.filePath("saved.txt")) &&
            QDir().mkdir(path) && workspace.root->child(entry).kind() == treemcp::NodeKind::Directory;
        return false;
    });
    if (!listed || !replaced || !std::holds_alternative<QString>(treemcp::readWorkspaceFile(workspace, name, {}))) {
        return 1;
    }
    std::puts(
        "A listed regular file was replaced during enumeration; its opened directory handle was rejected as source");
    return 0;
}
