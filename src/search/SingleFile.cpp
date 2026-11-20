#include "SingleFile.hpp"

#include "LanguageRegistry.hpp"
#include "Query.hpp"
#include "Workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QStringDecoder>

#include <algorithm>
#include <limits>

namespace treemcp::SingleFile {
namespace {

// Up to three serialized copies of an entry fit in the wire budget (structured JSON,
// its compact text, and escaping that text). Leave room for the envelope and request ID.
constexpr qsizetype ENTRY_BUDGET = 240 * 1024;
constexpr uint32_t MAX_TEXT_BYTES = 1024;
constexpr uint32_t MAX_CAPTURES = 64;

bool appendEntry(QJsonArray& entries, const QJsonObject& entry, qsizetype& remaining)
{
    const qsizetype bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact).size() + 1;
    if (bytes > remaining) {
        return false;
    }
    remaining -= bytes;
    entries.append(entry);
    return true;
}

QJsonObject range(TSNode node)
{
    const TSPoint start = ts_node_start_point(node);
    const TSPoint end = ts_node_end_point(node);
    return { { "startByte", qint64 { ts_node_start_byte(node) } },
             { "endByte", qint64 { ts_node_end_byte(node) } },
             { "startRow", qint64 { start.row } },
             { "startColumn", qint64 { start.column } },
             { "endRow", qint64 { end.row } },
             { "endColumn", qint64 { end.column } } };
}

QJsonObject dumpTree(TSNode root, int maxDepth, int maxNodes)
{
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    const auto guard = qScopeGuard([&cursor]() { ts_tree_cursor_delete(&cursor); });
    QJsonArray nodes;
    qsizetype remaining = ENTRY_BUDGET;
    bool truncated = false;
    int depth = 0;
    for (;;) {
        const TSNode node = ts_tree_cursor_current_node(&cursor);
        const QJsonObject entry { { "type", QString::fromUtf8(ts_node_type(node)) },
                                  { "range", range(node) },
                                  { "depth", depth },
                                  { "named", ts_node_is_named(node) },
                                  { "missing", ts_node_is_missing(node) } };
        if (nodes.size() == maxNodes || !appendEntry(nodes, entry, remaining)) {
            truncated = true;
            break;
        }
        if (depth < maxDepth && ts_tree_cursor_goto_first_child(&cursor)) {
            ++depth;
            continue;
        }
        if (depth == maxDepth && ts_node_child_count(node) > 0) {
            truncated = true;
        }
        while (depth > 0 && !ts_tree_cursor_goto_next_sibling(&cursor)) {
            ts_tree_cursor_goto_parent(&cursor);
            --depth;
        }
        if (depth == 0) {
            break;
        }
    }
    return { { "nodes", nodes }, { "hasSyntaxErrors", ts_node_has_error(root) }, { "truncated", truncated } };
}

QJsonObject captureEntry(const TSQueryCapture& capture, const TSQuery* query, QByteArrayView source, bool& truncated)
{
    const TSNode node = capture.node;
    const uint32_t start = ts_node_start_byte(node);
    const uint32_t size = ts_node_end_byte(node) - start;
    uint32_t length = std::min(size, MAX_TEXT_BYTES);
    while (length < size && (static_cast<unsigned char>(source[start + length]) & 0xc0U) == 0x80U) {
        --length;
    }
    const bool clipped = length < size;
    truncated |= clipped;
    uint32_t nameSize = 0;
    const char* name = ts_query_capture_name_for_id(query, capture.index, &nameSize);
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::ConvertInitialBom);
    const QString text = decoder.decode(source.sliced(start, length));
    return { { "name", QString::fromUtf8(name, nameSize) },
             { "range", range(node) },
             { "text", text },
             { "textTruncated", clipped } };
}

struct SearchResults {
    QJsonArray matches;
    qsizetype remaining = ENTRY_BUDGET;
    bool syntaxErrors = false;
    bool truncated = false;
    bool incomplete = false;
    bool full = false;

    QJsonObject data() const
    {
        return { { "matches", matches },
                 { "hasSyntaxErrors", syntaxErrors },
                 { "truncated", truncated },
                 { "incomplete", incomplete } };
    }
};

QString findMatches(
    TSNode root,
    QByteArrayView source,
    const CompiledQuery& compiled,
    int maxMatches,
    const QString& path,
    SearchResults& results)
{
    // The pinned runtime records up to three captures per visited node, but the C API
    // exposes only a 16-bit capture count. Guard repeated captures before it can wrap.
    constexpr uint32_t MAX_REPEAT_TREE_NODES = std::numeric_limits<uint16_t>::max() / 3;
    if (ts_node_descendant_count(root) > MAX_REPEAT_TREE_NODES) {
        const TSQuery* query = compiled.query.get();
        for (uint32_t pattern = 0; pattern < ts_query_pattern_count(query); ++pattern) {
            for (uint32_t id = 0; id < ts_query_capture_count(query); ++id) {
                const TSQuantifier quantifier = ts_query_capture_quantifier_for_id(query, pattern, id);
                if (quantifier == TSQuantifierOneOrMore || quantifier == TSQuantifierZeroOrMore) {
                    return QStringLiteral("Source tree exceeds the node limit for repeated captures");
                }
            }
        }
    }
    const Cursor cursor { ts_query_cursor_new(), ts_query_cursor_delete };
    if (!cursor) {
        return QStringLiteral("Cannot allocate query cursor");
    }
    ts_query_cursor_set_match_limit(cursor.get(), 256);
    ts_query_cursor_exec(cursor.get(), compiled.query.get(), root);
    TSQueryMatch match {};
    while (ts_query_cursor_next_match(cursor.get(), &match)) {
        const auto& predicates = compiled.predicates.at(match.pattern_index);
        if (!std::all_of(predicates.begin(), predicates.end(), [&](const Predicate& p) {
                return p.accepts(match, source);
            }))
        {
            continue;
        }
        if (results.matches.size() == maxMatches) {
            results.full = results.truncated = true;
            break;
        }
        QJsonArray captures;
        const bool clipped = match.capture_count > MAX_CAPTURES;
        results.truncated |= clipped;
        for (uint32_t i = 0; i < std::min<uint32_t>(match.capture_count, MAX_CAPTURES); ++i) {
            captures.append(captureEntry(match.captures[i], compiled.query.get(), source, results.truncated));
        }
        QJsonObject entry { { "patternIndex", match.pattern_index },
                            { "captures", captures },
                            { "capturesTruncated", clipped } };
        if (!path.isEmpty()) {
            entry.insert("path", path);
        }
        if (!appendEntry(results.matches, entry, results.remaining)) {
            results.full = results.truncated = true;
            break;
        }
    }
    results.incomplete |= ts_query_cursor_did_exceed_match_limit(cursor.get());
    return {};
}

Result parseSource(
    const QByteArray& source,
    const TSLanguage* language,
    const CompiledQuery& compiled,
    const QJsonObject& arguments,
    bool treeMode,
    const QString& path,
    SearchResults& results)
{
    const Parser parser { ts_parser_new(), ts_parser_delete };
    if (!parser || !ts_parser_set_language(parser.get(), language)) {
        return QStringLiteral("Cannot initialize parser");
    }
    const Tree tree {
        ts_parser_parse_string(parser.get(), nullptr, source.constData(), static_cast<uint32_t>(source.size())),
        ts_tree_delete
    };
    if (!tree) {
        return QStringLiteral("Cannot parse source");
    }
    const TSNode root = ts_tree_root_node(tree.get());
    if (treeMode) {
        return dumpTree(root, arguments.value("maxDepth").toInt(), arguments.value("maxNodes").toInt());
    }
    results.syntaxErrors |= ts_node_has_error(root);
    const QString error = findMatches(root, source, compiled, arguments.value("maxMatches").toInt(), path, results);
    if (!error.isEmpty()) {
        return error;
    }
    return QJsonObject {};
}

QStringList strings(const QJsonValue& value)
{
    QStringList list;
    for (const auto item : value.toArray()) {
        list.append(item.toString());
    }
    return list;
}

Result searchScope(
    const WorkspaceNode& workspace,
    const LanguageDefinition& grammar,
    const CompiledQuery& compiled,
    const QJsonObject& arguments)
{
    const FileScope scope { strings(arguments.value("paths")),
                            strings(arguments.value("include")),
                            strings(arguments.value("exclude")),
                            grammar.extensions };
    SearchResults results;
    QJsonArray diagnostics;
    const auto report = [&](const QString& path, const QString& message) {
        results.incomplete = true;
        if (diagnostics.size() < 16) {
            QString clipped = path.left(256);
            if (!clipped.isEmpty() && clipped.back().isHighSurrogate()) {
                clipped.chop(1);
            }
            diagnostics.append(QJsonObject { { "path", clipped }, { "message", message } });
        }
    };
    const QString error = scanWorkspace(
        workspace,
        scope,
        [&](const QString& path, const QByteArray& source) {
            const auto parsed = parseSource(source, grammar.language, compiled, arguments, false, path, results);
            if (const auto* failure = std::get_if<QString>(&parsed)) {
                report(path, *failure);
            }
            return !results.full;
        },
        report);
    if (!error.isEmpty()) {
        return error;
    }
    QJsonObject output = results.data();
    output.insert("diagnostics", diagnostics);
    return output;
}

} // namespace

Result run(const QString& name, const QJsonObject& arguments, const WorkspaceNode& workspace)
{
    const auto* grammar = LanguageRegistry::getLanguage(arguments.value("language").toString());
    if (!grammar) {
        return QStringLiteral("Unknown language");
    }
    CompiledQuery compiled;
    if (name != "syntax_tree") {
        const auto diagnostic = compiled.compile(grammar->language, arguments.value("query").toString());
        if (diagnostic) {
            if (name == "query_validate") {
                return QJsonObject { { "valid", false },
                                     { "diagnostic",
                                       QJsonObject { { "byteOffset", diagnostic->byteOffset },
                                                     { "message", diagnostic->message } } } };
            }
            return QStringLiteral("Query error at byte %1: %2").arg(diagnostic->byteOffset).arg(diagnostic->message);
        }
        if (name == "query_validate") {
            return QJsonObject { { "valid", true },
                                 { "patternCount", qint64 { ts_query_pattern_count(compiled.query.get()) } } };
        }
    }
    if (arguments.contains("paths")) {
        return searchScope(workspace, *grammar, compiled, arguments);
    }
    QByteArray source;
    if (arguments.contains("path")) {
        auto read = readWorkspaceFile(workspace, arguments.value("path").toString());
        if (const auto* error = std::get_if<QString>(&read)) {
            return *error;
        }
        source = std::get<QByteArray>(std::move(read));
    } else {
        source = arguments.value("source").toString().toUtf8();
    }
    SearchResults results;
    const Result output = parseSource(
        source,
        grammar->language,
        compiled,
        arguments,
        name == "syntax_tree",
        arguments.value("path").toString(),
        results);
    if (name == "syntax_tree" || std::holds_alternative<QString>(output)) {
        return output;
    }
    return results.data();
}

} // namespace treemcp::SingleFile
