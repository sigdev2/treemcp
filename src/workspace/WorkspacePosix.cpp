#include "WorkspaceNode.hpp"

#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <QFile>
#include <QScopeGuard>
#include <QStringDecoder>
#include <sys/stat.h>
#include <unistd.h>

namespace treemcp {

WorkspaceNode::~WorkspaceNode()
{
    if (native >= 0) {
        close(static_cast<int>(native));
    }
}

WorkspaceNode WorkspaceNode::openRoot(const QString& path)
{
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    return WorkspaceNode(fd >= 0 ? NodeKind::Directory : NodeKind::Error, fd);
}

WorkspaceNode WorkspaceNode::child(const QString& component) const
{
    if (nodeKind != NodeKind::Directory || !validComponent(component)) {
        return WorkspaceNode(NodeKind::Error);
    }
    const QByteArray name = component.toUtf8();
    struct stat entry {};
    if (fstatat(static_cast<int>(native), name.constData(), &entry, AT_SYMLINK_NOFOLLOW) < 0) {
        return WorkspaceNode(errno == ENOENT ? NodeKind::Missing : NodeKind::Error);
    }
    if (!S_ISREG(entry.st_mode) && !S_ISDIR(entry.st_mode)) {
        return WorkspaceNode(NodeKind::Excluded);
    }
    const int fd =
        openat(static_cast<int>(native), name.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | O_NOCTTY);
    if (fd < 0) {
        return WorkspaceNode(errno == ENOENT ? NodeKind::Missing : NodeKind::Error);
    }
    struct stat opened {};
    if (fstat(fd, &opened) < 0 || opened.st_dev != entry.st_dev || opened.st_ino != entry.st_ino ||
        (!S_ISREG(opened.st_mode) && !S_ISDIR(opened.st_mode)))
    {
        close(fd);
        return WorkspaceNode(NodeKind::Error);
    }
    return WorkspaceNode(S_ISDIR(opened.st_mode) ? NodeKind::Directory : NodeKind::File, fd);
}

bool WorkspaceNode::list(const EntryVisitor& visit) const
{
    // A new open file description gives this iterator an independent offset.
    const int fd = openat(static_cast<int>(native), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    DIR* directory = fdopendir(fd);
    if (!directory) {
        close(fd);
        return false;
    }
    const auto guard = qScopeGuard([directory]() { closedir(directory); });
    for (;;) {
        errno = 0;
        const dirent* entry = readdir(directory);
        if (!entry) {
            return errno == 0;
        }
        const QByteArrayView name(entry->d_name);
        QStringDecoder decoder(
            QStringDecoder::Utf8,
            QStringDecoder::Flag::ConvertInitialBom | QStringDecoder::Flag::Stateless);
        const QString decoded = decoder.decode(name);
        unsigned int type = entry->d_type;
        if (type == DT_UNKNOWN) {
            struct stat info {};
            if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0) {
                type = IFTODT(info.st_mode);
            }
        }
        const NodeKind kind = type == DT_DIR ? NodeKind::Directory
            : type == DT_REG                 ? NodeKind::File
                                             : NodeKind::Excluded;
        if (!visit(decoder.hasError() ? QString() : decoded, kind)) {
            return true;
        }
    }
}

std::optional<FileStamp> WorkspaceNode::stamp() const
{
    struct stat info {};
    if (fstat(static_cast<int>(native), &info) < 0 || !S_ISREG(info.st_mode)) {
        return std::nullopt;
    }
#ifdef Q_OS_MACOS
    const auto modified = info.st_mtimespec;
    const auto changed = info.st_ctimespec;
#else
    const auto modified = info.st_mtim;
    const auto changed = info.st_ctim;
#endif
    return FileStamp { info.st_dev,    info.st_ino,      info.st_size,   modified.tv_sec,
                       changed.tv_sec, modified.tv_nsec, changed.tv_nsec };
}

qsizetype WorkspaceNode::read(char* buffer, qsizetype size) const
{
    ssize_t count;
    do {
        count = ::read(static_cast<int>(native), buffer, static_cast<size_t>(size));
    } while (count < 0 && errno == EINTR);
    return count;
}

} // namespace treemcp
