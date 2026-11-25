#include "Scheduler.hpp"
#include "Tools.hpp"
#include "Workspace.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
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
bool failParse = false;

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
    if (failParse) {
        return nullptr;
    }
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
        if (i == 1) {
            // Expiry between worker completion and publication must preserve all retained results.
            auto expected = data;
            auto items = expected.value("items").toArray();
            for (qsizetype index = 0; index < items.size(); ++index) {
                auto item = items.at(index).toObject();
                item.insert("incomplete", true);
                items[index] = item;
            }
            expected.insert("items", items);
            expected.insert("incomplete", true);
            expected.insert("deadlineExceeded", true);
            const auto late = treemcp::Tools::deadlineReply(1, treemcp::Tools::dataReply(1, data));
            const auto result = late.value("result").toObject();
            const auto text = result.value("content").toArray().first().toObject().value("text").toString();
            if (late.value("id") != 1 || result.value("structuredContent") != expected ||
                QJsonDocument::fromJson(text.toUtf8()).object() != expected)
            {
                return 1;
            }
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
    args.insert("paths", QJsonArray { "a.py" });
    args.insert("queries", QJsonArray { queries.first(), queries.at(1) });
    failParse = true;
    const auto failed = call().value("structuredContent").toObject();
    failParse = false;
    const auto affected = failed.value("items").toArray();
    const QJsonArray diagnostic { QJsonObject { { "path", "a.py" }, { "message", "Cannot parse source" } } };
    if (affected.size() != 2 || failed.value("incomplete") != true || failed.value("diagnostics") != diagnostic ||
        affected.first().toObject().value("incomplete") != true ||
        affected.last().toObject().value("incomplete") != false ||
        failed.value("stats").toObject().value("treeBuilds") != 0 || liveTrees != 0)
    {
        return 1;
    }
    // Make the last worker cross the deadline after it has already retained a match.
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
