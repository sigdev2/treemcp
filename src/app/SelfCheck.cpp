#include "SelfCheck.hpp"

#include "LanguageRegistry.hpp"

#include <QByteArray>
#include <tree_sitter/api.h>

#include <cstdint>
#include <memory>

namespace treemcp::SelfCheck {

namespace {

using Parser = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using Tree = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;
using Query = std::unique_ptr<TSQuery, decltype(&ts_query_delete)>;
using Cursor = std::unique_ptr<TSQueryCursor, decltype(&ts_query_cursor_delete)>;

QString checkGrammar(const LanguageDefinition& definition, const QByteArray& source)
{
    const Parser parser { ts_parser_new(), &ts_parser_delete };
    if (parser == nullptr || !ts_parser_set_language(parser.get(), definition.language)) {
        return QStringLiteral("Cannot initialize the parser or load the grammar ABI.");
    }

    const Tree tree {
        ts_parser_parse_string(parser.get(), nullptr, source.constData(), static_cast<uint32_t>(source.size())),
        &ts_tree_delete
    };
    if (tree == nullptr || ts_node_has_error(ts_tree_root_node(tree.get()))) {
        return QStringLiteral("The grammar did not parse the valid fixture.");
    }

    const QByteArray querySource("(function_definition) @definition");
    uint32_t errorOffset = 0;
    TSQueryError errorType = TSQueryErrorNone;
    const Query query { ts_query_new(
                            definition.language,
                            querySource.constData(),
                            static_cast<uint32_t>(querySource.size()),
                            &errorOffset,
                            &errorType),
                        &ts_query_delete };
    if (query == nullptr) {
        return QStringLiteral("Cannot compile the fixture query at byte %1.").arg(errorOffset);
    }

    const Cursor cursor { ts_query_cursor_new(), &ts_query_cursor_delete };
    if (cursor == nullptr) {
        return QStringLiteral("Cannot allocate a query cursor.");
    }
    ts_query_cursor_exec(cursor.get(), query.get(), ts_tree_root_node(tree.get()));

    TSQueryMatch match {};
    if (!ts_query_cursor_next_match(cursor.get(), &match) || match.capture_count != 1) {
        return QStringLiteral("Expected exactly one capture in the fixture match.");
    }
    const TSNode node = match.captures[0].node;
    const uint32_t expectedEnd = static_cast<uint32_t>(source.trimmed().size());
    if (ts_node_start_byte(node) != 0 || ts_node_end_byte(node) != expectedEnd) {
        return QStringLiteral("The captured function has unexpected byte boundaries.");
    }
    if (ts_query_cursor_next_match(cursor.get(), &match)) {
        return QStringLiteral("The fixture produced an unexpected second match.");
    }

    const QByteArray invalidSource("(treemcp_nonexistent_node) @node");
    const Query invalidQuery { ts_query_new(
                                   definition.language,
                                   invalidSource.constData(),
                                   static_cast<uint32_t>(invalidSource.size()),
                                   &errorOffset,
                                   &errorType),
                               &ts_query_delete };
    if (invalidQuery != nullptr || errorType != TSQueryErrorNodeType) {
        return QStringLiteral("The grammar accepted an unknown query node type.");
    }
    return {};
}

} // namespace

QStringList run()
{
    QStringList failures;
    const QList<LanguageDefinition>& languages = LanguageRegistry::getLanguages();
    if (languages.size() != 2 || LanguageRegistry::getLanguage(QStringLiteral("unknown")) != nullptr) {
        failures.append(QStringLiteral("The bootstrap language registry is inconsistent."));
    }
    for (const LanguageDefinition& definition : languages) {
        const QByteArray source = definition.id == QStringLiteral("cpp") ? QByteArray("int answer() { return 42; }\n")
                                                                         : QByteArray("def answer():\n    return 42\n");
        const QString failure = checkGrammar(definition, source);
        if (!failure.isEmpty()) {
            failures.append(definition.id + QStringLiteral(": ") + failure);
        }
    }
    return failures;
}

} // namespace treemcp::SelfCheck
