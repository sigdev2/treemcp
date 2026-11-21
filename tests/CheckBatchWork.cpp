#include "Scheduler.hpp"
#include "Tools.hpp"
#include "Workspace.hpp"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <tree_sitter/api.h>
#include <unistd.h>

namespace {

std::atomic_int reads = 0;
std::atomic_int builds = 0;
std::atomic_int liveTrees = 0;
std::atomic_bool ownershipError = false;
// Each text file in this fixture is parsed exactly once before its worker takes another job.
thread_local bool hasSource = false;
thread_local const TSTree* ownedTree = nullptr;
using Clock = std::chrono::steady_clock;
Clock::time_point delayCleanupUntil {};

} // namespace

extern "C" ssize_t __real_read(int fd, void* buffer, size_t size);
extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size);
extern "C" TSTree* __real_ts_parser_parse_with_options(TSParser*, const TSTree*, TSInput, TSParseOptions);
extern "C" TSTree* __wrap_ts_parser_parse_with_options(TSParser*, const TSTree*, TSInput, TSParseOptions);
extern "C" void __real_ts_tree_delete(TSTree* tree);
extern "C" void __wrap_ts_tree_delete(TSTree* tree);

extern "C" ssize_t __wrap_read(int fd, void* buffer, size_t size)
{
    if (lseek(fd, 0, SEEK_CUR) == 0) {
        if (hasSource || ownedTree) {
            ownershipError = true;
        }
        hasSource = true;
        ++reads;
    }
    return __real_read(fd, buffer, size);
}

extern "C" TSTree*
    __wrap_ts_parser_parse_with_options(TSParser* parser, const TSTree* previous, TSInput input, TSParseOptions options)
{
    if (!hasSource || ownedTree) {
        ownershipError = true;
    }
    hasSource = false;
    ++builds;
    TSTree* tree = __real_ts_parser_parse_with_options(parser, previous, input, options);
    if (tree) {
        ownedTree = tree;
        ++liveTrees;
    }
    return tree;
}

extern "C" void __wrap_ts_tree_delete(TSTree* tree)
{
    if (tree) {
        if (tree != ownedTree) {
            ownershipError = true;
        }
        if (delayCleanupUntil != Clock::time_point {}) {
            std::this_thread::sleep_until(delayCleanupUntil);
        }
        ownedTree = nullptr;
        --liveTrees;
    }
    __real_ts_tree_delete(tree);
}

int main()
{
    QTemporaryDir directory;
    for (const auto& name : { "a.py", "b.py", "c.cpp" }) {
        QFile file(directory.filePath(QString::fromLatin1(name)));
        if (!file.open(QIODevice::WriteOnly) || file.write("x") != 1) {
            return 1;
        }
    }
    const treemcp::Workspace workspace { treemcp::WorkspaceNode::openRoot(directory.path()), {} };
    QJsonArray queries;
    for (int i = 0; i < 16; ++i) {
        queries.append(QJsonObject { { "id", QString::number(i) },
                                     { "language", i % 2 ? "cpp" : "python" },
                                     { "query", "(identifier) @name" } });
    }
    QJsonObject args { { "paths", QJsonArray { ".", "a.py", "." } }, { "queries", queries } };
    const std::atomic_bool cancelled = false;
    treemcp::Scheduler scheduler(4);
    auto deadline = Clock::now() + std::chrono::seconds(30);
    const auto call = [&] {
        return treemcp::Tools::call(
                   1,
                   { { "name", "batch_search" }, { "arguments", args } },
                   [&](const QString& name, const QJsonObject& admitted) {
                       return treemcp::Tools::execute(1, name, admitted, workspace, { cancelled, deadline }, scheduler);
                   })
            ->value("result")
            .toObject();
    };
    for (int i = 1; i <= 2; ++i) {
        const auto data = call().value("structuredContent").toObject();
        if (!data.contains("stats") || data.value("incomplete").toBool() || reads != i * 3 || builds != i * 3 ||
            liveTrees != 0 || ownershipError.load())
        {
            return 1;
        }
    }
    args.insert("paths", QJsonArray { ".", "../invalid" });
    if (!call().value("isError").toBool()) {
        return 1;
    }
    args.insert("paths", QJsonArray { "." });
    args.insert("queries", QJsonArray { queries.first(), queries.first() });
    if (!call().value("isError").toBool() || reads != 6 || builds != 6 || liveTrees != 0 || ownershipError.load()) {
        return 1;
    }
    // Make the last worker cross the deadline after it has already retained a match.
    args.insert("paths", QJsonArray { "a.py" });
    args.insert("queries", QJsonArray { queries.first() });
    deadline = Clock::now() + std::chrono::milliseconds(500);
    delayCleanupUntil = deadline;
    const auto partial = call().value("structuredContent").toObject();
    const auto items = partial.value("items").toArray();
    if (items.size() != 1 || items.first().toObject().value("matches").toArray().size() != 1 ||
        !partial.value("incomplete").toBool() || !partial.value("deadlineExceeded").toBool() || liveTrees != 0 ||
        ownershipError.load())
    {
        return 1;
    }
    return 0;
}
