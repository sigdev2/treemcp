#include "SelfCheck.hpp"

#include "LanguageRegistry.hpp"
#include "Query.hpp"

#include <QByteArray>
#include <tree_sitter/api.h>

#include <cstdint>
#include <iterator>

namespace treemcp::SelfCheck {

namespace {

QString checkGrammar(const LanguageDefinition& definition, const QByteArray& source, const QByteArray& querySource)
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
        return QStringLiteral("The captured node has unexpected byte boundaries.");
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
    struct Fixture {
        const char* id;
        const char* source;
        const char* query;
    };
    constexpr Fixture fixtures[] {
        { "cpp", "template<class T> T answer(T x) { return x; }", "(template_declaration) @node" },
        { "python", "def answer():\n    return 42", "(function_definition) @node" },
        { "c", "int answer(void) { return _Generic(1, int: 42); }", "(function_definition) @node" },
        { "rust", "fn answer() -> i32 { 42 }", "(function_item) @node" },
        { "javascript", "function answer() { return 42; }", "(function_declaration) @node" },
        { "typescript", "interface Answer { value: number }", "(interface_declaration) @node" },
        { "tsx", "const answer = <Answer value={42} />;", "(lexical_declaration) @node" },
        { "go", "package answer", "(package_clause) @node" },
        { "java", "class Answer { int value() { return 42; } }", "(class_declaration) @node" }
    };
    if (std::ssize(fixtures) != LanguageRegistry::getLanguages().size()) {
        failures.append(QStringLiteral("Expected one self-check fixture per registered grammar."));
    }
    for (const Fixture& fixture : fixtures) {
        const auto id = QString::fromLatin1(fixture.id);
        const auto* definition = LanguageRegistry::getLanguage(id);
        const QString failure = definition ? checkGrammar(*definition, fixture.source, fixture.query)
                                           : QStringLiteral("Missing registered grammar.");
        if (!failure.isEmpty()) {
            failures.append(id + QStringLiteral(": ") + failure);
        }
    }
    return failures;
}

} // namespace treemcp::SelfCheck
