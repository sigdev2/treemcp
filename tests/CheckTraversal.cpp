#include "Workspace.hpp"

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <QFile>
#include <QTemporaryDir>
#include <sys/stat.h>

namespace {

// Supply an ordered directory stream without creating 100,000 files on disk.
// The final entry is a real file whose filesystem type must be resolved by stat.
int markerPosition = 0;
int listed = 0;
int endError = 0;
bool changeIdentity = false;

} // namespace

extern "C" int __real_fstat(int fd, struct stat* info);
extern "C" int __wrap_fstat(int fd, struct stat* info);

extern "C" int __wrap_fstat(int fd, struct stat* info)
{
    const int status = __real_fstat(fd, info);
    if (status == 0 && changeIdentity && S_ISREG(info->st_mode)) {
        // Model an opened regular file differing from the preceding fstatat observation.
        ++info->st_ino;
        changeIdentity = false;
    }
    return status;
}

extern "C" dirent* __real_readdir(DIR* directory);
extern "C" dirent* __wrap_readdir(DIR* directory);

extern "C" dirent* __wrap_readdir(DIR* directory)
{
    if (!markerPosition) {
        return __real_readdir(directory);
    }
    if (listed == markerPosition) {
        errno = endError;
        return nullptr;
    }
    static dirent entry {};
    ++listed;
    if (listed == markerPosition) {
        entry.d_type = DT_UNKNOWN;
        std::snprintf(entry.d_name, sizeof(entry.d_name), "marker.py");
    } else {
        entry.d_type = DT_REG;
        std::snprintf(entry.d_name, sizeof(entry.d_name), "skip%d.txt", listed);
    }
    return &entry;
}

int main()
{
    QTemporaryDir directory;
    QFile marker(directory.filePath("marker.py"));
    if (!directory.isValid() || !marker.open(QIODevice::WriteOnly) || marker.write("present") != 7) {
        return 1;
    }
    marker.close();
    const treemcp::Workspace workspace { treemcp::WorkspaceNode::openRoot(directory.path()), {} };
    changeIdentity = true;
    if (workspace.root->child(QStringLiteral("marker.py")).kind() != treemcp::NodeKind::Error || changeIdentity) {
        return 1;
    }
    std::puts("POSIX rejected a mismatched inode at opened-handle validation");
    const treemcp::FileScope scope { { "." }, {}, {}, { ".py" } };
    const auto scan =
        [&](int position, int error, const QStringList& expectedMessages, const QStringList& expectedFiles) {
            markerPosition = position;
            endError = error;
            listed = 0;
            QStringList files;
            QStringList messages;
            const auto problem = treemcp::scanWorkspace(
                workspace,
                scope,
                [&](const QString& path, treemcp::SelectedFile file) {
                    const auto data = file.read({});
                    const auto* bytes = std::get_if<QByteArray>(&data);
                    files.append(bytes && *bytes == "present" ? path : QStringLiteral("unexpected content"));
                    return true;
                },
                [&](const QString& path, const QString& message, treemcp::ScanFailure failure) {
                    messages.append(failure == treemcp::ScanFailure::Scope ? path + ": " + message : message);
                });
            markerPosition = 0;
            return problem.isEmpty() && files == expectedFiles && messages == expectedMessages && listed == position;
        };
    return scan(100000, 0, {}, { "marker.py" }) && scan(100001, 0, { ".: Directory entry limit exceeded" }, {}) &&
            scan(1, EIO, { ".: Cannot enumerate workspace directory" }, { "marker.py" })
        ? 0
        : 1;
}
