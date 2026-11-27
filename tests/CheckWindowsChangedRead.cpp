#include "Workspace.hpp"

#include <cstdio>
#include <QFile>
#include <QTemporaryDir>
#include <windows.h>

int main()
{
    QTemporaryDir directory;
    const QString name = QStringLiteral("source.py");
    QFile file(directory.filePath(name));
    if (!directory.isValid() || !file.open(QIODevice::WriteOnly) || file.write("abc") != 3) {
        return 1;
    }
    file.close();
    const auto path = file.fileName().toStdWString();
    const auto root = treemcp::WorkspaceNode::openRoot(directory.path());
    const auto node = root.child(name);
    constexpr DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const HANDLE writer = CreateFileW(path.c_str(), FILE_WRITE_DATA, sharing, nullptr, OPEN_EXISTING, 0, nullptr);
    if (writer != INVALID_HANDLE_VALUE) {
        CloseHandle(writer);
        return 1;
    }
    if (GetLastError() != ERROR_SHARING_VIOLATION || node.kind() != treemcp::NodeKind::File) {
        return 1;
    }
    // Data writers are excluded. Windows permits a separate attributes-only handle.
    // Change the stamp after inspection, before reading the first chunk on each attempt.
    const HANDLE attributes =
        CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, sharing, nullptr, OPEN_EXISTING, 0, nullptr);
    if (attributes == INVALID_HANDLE_VALUE) {
        return 1;
    }
    bool ok = true;
    FILETIME stamp {};
    GetSystemTimeAsFileTime(&stamp);
    for (int changes = 0; changes <= 2; ++changes) {
        int checkpoints = 0;
        const auto result = treemcp::readText(root, name, root.child(name), 1024, [&] {
            // Three bytes and then EOF give two read checkpoints per attempt.
            if (checkpoints % 2 == 0 && checkpoints / 2 < changes) {
                ++stamp.dwLowDateTime;
                ok &= SetFileTime(attributes, nullptr, nullptr, &stamp) != 0;
            }
            ++checkpoints;
            return false;
        });
        const auto* error = std::get_if<QString>(&result);
        const auto* bytes = std::get_if<QByteArray>(&result);
        ok &= changes == 2 ? error && *error == "Workspace file changed during read" : bytes && *bytes == "abc";
        ok &= checkpoints == (changes == 0 ? 2 : 4);
    }
    CloseHandle(attributes);
    if (ok) {
        std::puts("Windows denied a concurrent data writer; stable, once-changed and twice-changed stamps verified");
    }
    return ok ? 0 : 1;
}
