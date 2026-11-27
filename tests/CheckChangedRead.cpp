#include "Workspace.hpp"

#include <cerrno>
#include <cstdio>
#include <QFile>
#include <QTemporaryDir>
#include <unistd.h>

namespace {

int writer = -1;
int attempts = 0;
int mutations = 0;
int readError = 0;

} // namespace

extern "C" ssize_t __real_read(int fd, void* buffer, size_t size);
extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size);

// Interpose only in this test executable. Mutate a real file after its initial
// fstat and before the first data read of each selected attempt.
extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size)
{
    if (writer >= 0 && lseek(fd, 0, SEEK_CUR) == 0) {
        ++attempts;
        if (attempts == 1 && readError != 0) {
            errno = readError;
            return -1;
        }
        if (attempts <= mutations && write(writer, "x", 1) != 1) {
            return -1;
        }
    }
    return __real_read(fd, buffer, size);
}

int main()
{
    QTemporaryDir directory;
    QFile file(directory.filePath("source.py"));
    if (!directory.isValid() || !file.open(QIODevice::WriteOnly) || file.write("a") != 1 || !file.flush()) {
        return 1;
    }
    const auto root = treemcp::WorkspaceNode::openRoot(directory.path());
    const QString name = QStringLiteral("source.py");
    writer = file.handle();
    for (mutations = 0; mutations <= 2; ++mutations) {
        attempts = 0;
        const auto node = root.child(name);
        if (node.kind() != treemcp::NodeKind::File) {
            return 1;
        }
        const auto result = treemcp::readText(root, name, node, 1024);
        if (mutations == 2) {
            const auto* error = std::get_if<QString>(&result);
            if (attempts != 2 || !error || !error->contains("changed")) {
                return 1;
            }
        } else {
            const auto* bytes = std::get_if<QByteArray>(&result);
            if (attempts != mutations + 1 || !bytes || *bytes != (mutations ? "ax" : "a")) {
                return 1;
            }
        }
    }
    mutations = 0;
    attempts = 0;
    readError = EINTR;
    const auto interrupted = treemcp::readText(root, name, root.child(name), 1024);
    const auto* bytes = std::get_if<QByteArray>(&interrupted);
    if (!bytes || *bytes != "axxx" || attempts != 2) {
        return 1;
    }
    attempts = 0;
    readError = EIO;
    const auto failed = treemcp::readText(root, name, root.child(name), 1024);
    const auto* error = std::get_if<QString>(&failed);
    if (!error || *error != "Cannot read workspace file" || attempts != 1) {
        return 1;
    }
    attempts = 0;
    const auto stopped = treemcp::readText(root, name, root.child(name), 1024, [] { return true; });
    const auto* cancelled = std::get_if<QString>(&stopped);
    if (!cancelled || *cancelled != "Workspace read interrupted" || attempts != 0) {
        return 1;
    }
    writer = -1;
    std::puts("POSIX stable read, append and retry, repeated change, EINTR, I/O error and interruption exercised");
    return 0;
}
