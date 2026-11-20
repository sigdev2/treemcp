#include "BatchSearch.hpp"
#include "Tools.hpp"
#include "WorkspaceNode.hpp"

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <tree_sitter/api.h>
#include <unistd.h>

namespace {

std::atomic_int reads = 0;
std::atomic_int builds = 0;
std::atomic_int liveTrees = 0;

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
        ++reads;
    }
    return __real_read(fd, buffer, size);
}

extern "C" TSTree*
    __wrap_ts_parser_parse_with_options(TSParser* parser, const TSTree* previous, TSInput input, TSParseOptions options)
{
    ++builds;
    TSTree* tree = __real_ts_parser_parse_with_options(parser, previous, input, options);
    if (tree) {
        ++liveTrees;
    }
    return tree;
}

extern "C" void __wrap_ts_tree_delete(TSTree* tree)
{
    if (tree) {
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
    const auto root = treemcp::WorkspaceNode::openRoot(directory.path());
    QJsonArray queries;
    for (int i = 0; i < 16; ++i) {
        queries.append(QJsonObject { { "id", QString::number(i) },
                                     { "language", i % 2 ? "cpp" : "python" },
                                     { "query", "(identifier) @name" } });
    }
    QJsonObject args { { "paths", QJsonArray { ".", "a.py", "." } }, { "queries", queries } };
    const std::atomic_bool cancelled = false;
    const auto call = [&] {
        return treemcp::Tools::call(
                   1,
                   { { "name", "batch_search" }, { "arguments", args } },
                   root,
                   [&](const QJsonObject& admitted) {
                       return treemcp::Tools::batchReply(
                           1,
                           admitted,
                           root,
                           { cancelled, 4, std::chrono::steady_clock::now() });
                   })
            ->value("result")
            .toObject();
    };
    for (int i = 1; i <= 2; ++i) {
        const auto data = call().value("structuredContent").toObject();
        if (!data.contains("stats") || data.value("incomplete").toBool() || reads != i * 3 || builds != i * 3 ||
            liveTrees != 0)
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
    if (!call().value("isError").toBool() || reads != 6 || builds != 6 || liveTrees != 0) {
        return 1;
    }
    return 0;
}
