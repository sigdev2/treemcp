#include "Scheduler.hpp"
#include "Tools.hpp"
#include "Workspace.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QSemaphore>
#include <QTemporaryDir>
#include <tree_sitter/api.h>

#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <future>
#include <sys/stat.h>
#include <unistd.h>

namespace {

ino_t slowFile = 0;
ino_t fastFile = 0;
ino_t readError = 0;
bool gate = false;
QSemaphore waiting;
QSemaphore finishedFast;
QSemaphore resume;
thread_local ino_t reading = 0;

struct ScanTail {
    treemcp::Scheduler& scheduler;
    treemcp::FileJobs& files;
    int listed = 0;
};
thread_local ScanTail* tail = nullptr;

ino_t inode(const QString& file)
{
    struct stat info {};
    return stat(QFile::encodeName(file).constData(), &info) == 0 ? info.st_ino : 0;
}

qsizetype descriptors()
{
    return QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot).size();
}

QJsonObject search(
    const treemcp::Workspace& workspace,
    treemcp::Scheduler& scheduler,
    treemcp::FileJobs& files,
    const std::atomic_bool& cancelled,
    int limit)
{
    const QJsonObject arguments { { "paths", QJsonArray { ".", "missing.py" } },
                                  { "language", "python" },
                                  { "query", "(identifier) @name" },
                                  { "maxMatches", limit } };
    const auto reply = treemcp::Tools::call(
        1,
        { { "name", "structural_search" }, { "arguments", arguments } },
        [&](const QString& name, const QJsonObject& validated) {
            return treemcp::Tools::execute(
                1,
                name,
                validated,
                workspace,
                { cancelled, std::chrono::steady_clock::now() + std::chrono::seconds(15) },
                scheduler,
                &files);
        });
    return reply->value("result").toObject().value("structuredContent").toObject();
}

} // namespace

extern "C" ssize_t __real_read(int fd, void* buffer, size_t size);
extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size);
extern "C" void __real_ts_tree_delete(TSTree* tree);
extern "C" void __wrap_ts_tree_delete(TSTree* tree);
extern "C" dirent* __real_readdir(DIR* directory);
extern "C" dirent* __wrap_readdir(DIR* directory);

extern "C" dirent* __wrap_readdir(DIR* directory)
{
    if (!tail) {
        return __real_readdir(directory);
    }
    if (++tail->listed == 2) {
        // Let the first file finish only after the scanner has asked for an unrelated entry.
        resume.release();
        tail->scheduler.joinFiles(tail->files);
    }
    if (tail->listed > 8) {
        errno = 0;
        return nullptr;
    }
    static thread_local dirent entry {};
    entry.d_type = DT_REG;
    std::snprintf(entry.d_name, sizeof(entry.d_name), "%s", tail->listed == 1 ? "first.py" : "ignored.txt");
    return &entry;
}

extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size)
{
    if (lseek(fd, 0, SEEK_CUR) == 0) {
        struct stat info {};
        if (fstat(fd, &info) == 0) {
            reading = info.st_ino;
            if (reading == readError) {
                errno = EIO;
                return -1;
            }
            if (gate && reading == slowFile) {
                waiting.release();
                resume.acquire();
            }
        }
    }
    return __real_read(fd, buffer, size);
}

extern "C" void __wrap_ts_tree_delete(TSTree* tree)
{
    const bool fast = gate && tree && reading == fastFile;
    __real_ts_tree_delete(tree);
    if (fast) {
        finishedFast.release();
    }
}

int main()
{
    QTemporaryDir directory;
    const QByteArray source = "alpha\nbeta\n#" + QByteArray(2048, ' ') + '\n';
    for (const auto& name : { QStringLiteral("first.py"), QStringLiteral("second.py") }) {
        QFile file(directory.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size()) {
            return 1;
        }
    }
    const treemcp::Workspace workspace { treemcp::WorkspaceNode::openRoot(directory.path()), {} };
    treemcp::Scheduler serial(1);
    treemcp::Scheduler parallel(4);
    std::atomic_bool cancelled = false;
    treemcp::FileJobs serialFiles;
    const auto oracle = search(workspace, serial, serialFiles, cancelled, 4);
    const auto matches = oracle.value("matches").toArray();
    if (matches.size() != 4 || oracle.value("truncated") != false || oracle.value("incomplete") != false) {
        return 1;
    }
    slowFile = inode(directory.filePath(matches.at(0).toObject().value("path").toString()));
    fastFile = inode(directory.filePath(matches.at(2).toObject().value("path").toString()));
    if (!slowFile || !fastFile || slowFile == fastFile) {
        return 1;
    }

    const auto before = descriptors();
    for (const bool cancel : { false, true }) {
        gate = true;
        treemcp::FileJobs files;
        auto result = std::async(std::launch::async, [&] { return search(workspace, parallel, files, cancelled, 4); });
        const bool reversed = waiting.tryAcquire(1, 3000) && finishedFast.tryAcquire(1, 3000);
        if (cancel) {
            cancelled = true;
            parallel.cancelFiles(files);
        }
        resume.release();
        const auto data = result.get();
        gate = false;
        if (!reversed || (cancel ? !data.isEmpty() : data != oracle) || descriptors() != before) {
            return 1;
        }
    }
    cancelled = false;
    readError = slowFile;
    treemcp::FileJobs files;
    const auto failed = search(workspace, parallel, files, cancelled, 3);
    if (failed != search(workspace, serial, serialFiles, cancelled, 3) || failed.value("incomplete") != true ||
        failed.value("diagnostics").toArray().size() != 1 || descriptors() != before)
    {
        return 1;
    }
    readError = 0;
    treemcp::FileJobs reused;
    if (search(workspace, parallel, reused, cancelled, 4) != oracle || descriptors() != before) {
        return 1;
    }
    treemcp::FileJobs prefixFiles;
    ScanTail scan { parallel, prefixFiles };
    slowFile = inode(directory.filePath("first.py"));
    tail = &scan;
    gate = true;
    const auto prefix = search(workspace, parallel, prefixFiles, cancelled, 1);
    gate = false;
    tail = nullptr;
    return scan.listed == 2 && prefix.value("matches").toArray().size() == 1 && prefix.value("truncated") == true &&
            prefix.value("incomplete") == false && descriptors() == before
        ? 0
        : 1;
}
