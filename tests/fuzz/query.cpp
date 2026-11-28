#include "LanguageRegistry.hpp"
#include "Matches.hpp"

#include <QJsonDocument>

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1 || size > 8192) {
        return 0;
    }
    const QByteArrayView input(reinterpret_cast<const char*>(data + 1), static_cast<qsizetype>(size - 1));
    const auto split = input.indexOf('\0');
    const auto query = split < 0 ? input : input.first(split);
    const auto* language = treemcp::LanguageRegistry::getLanguage(data[0] % 2 ? "cpp" : "python")->language;
    treemcp::CompiledQuery compiled;
    if (query.size() > 2048 || compiled.compile(language, QString::fromUtf8(query))) {
        return 0;
    }
    // Match the valid UTF-8 source contract of the inline tool.
    const QByteArray source = split < 0 ? QByteArray("x = x") : QString::fromUtf8(input.sliced(split + 1)).toUtf8();
    const std::atomic_bool cancelled = false;
    const treemcp::SearchStop stop { cancelled, std::chrono::steady_clock::now() + std::chrono::milliseconds(100) };
    const auto tree = treemcp::parseTree(source, language, stop);
    if (tree) {
        int visited = 0;
        bool incomplete = false;
        treemcp::visitMatches(
            ts_tree_root_node(tree.get()),
            source,
            compiled,
            [&](const TSQueryMatch& match, bool) {
                const auto entry = treemcp::copyMatch(match, compiled.query.get(), source, "fixture", "fixture");
                const auto response = entry.json();
                if (QJsonDocument::fromJson(QJsonDocument(response).toJson(QJsonDocument::Compact)).object() !=
                    response) {
                    std::abort();
                }
                return ++visited < 32;
            },
            incomplete,
            stop);
    }
    return 0;
}
