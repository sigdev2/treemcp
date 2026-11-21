#include "SingleFile.hpp"

#include "LanguageRegistry.hpp"
#include "Matches.hpp"
#include "Workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>

#include <algorithm>

namespace treemcp::SingleFile {
namespace {

// Up to three serialized copies of an entry fit in the wire budget (structured JSON,
// its compact text, and escaping that text). Leave room for the envelope and request ID.
constexpr qsizetype ENTRY_BUDGET = 240 * 1024;
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

QJsonObject dumpTree(TSNode root, int maxDepth, int maxNodes, const SearchStop& stop)
{
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    const auto guard = qScopeGuard([&cursor]() { ts_tree_cursor_delete(&cursor); });
    QJsonArray nodes;
    qsizetype remaining = ENTRY_BUDGET;
    bool truncated = false;
    int depth = 0;
    while (!stop.requested()) {
        const TSNode node = ts_tree_cursor_current_node(&cursor);
        const QJsonObject entry { { "type", QString::fromUtf8(ts_node_type(node)) },
                                  { "range", nodeRange(node) },
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

Result parseSource(
    const QByteArray& source,
    const TSLanguage* language,
    const CompiledQuery& compiled,
    const QJsonObject& arguments,
    bool treeMode,
    const QString& path,
    SearchResults& results,
    const SearchStop& stop)
{
    const Tree tree = parseTree(source, language, stop);
    if (!tree) {
        return QStringLiteral("Cannot parse source");
    }
    const TSNode root = ts_tree_root_node(tree.get());
    if (treeMode) {
        return dumpTree(root, arguments.value("maxDepth").toInt(), arguments.value("maxNodes").toInt(), stop);
    }
    results.syntaxErrors |= ts_node_has_error(root);
    const int maxMatches = arguments.value("maxMatches").toInt();
    const QString error = visitMatches(
        root,
        source,
        compiled,
        [&](const TSQueryMatch& match) {
            if (results.matches.size() < maxMatches) {
                const SearchMatch entry = copyMatch(match, compiled.query.get(), source, path, {});
                results.truncated |= entry.clipped();
                if (appendEntry(results.matches, entry.json(), results.remaining)) {
                    return true;
                }
            }
            results.full = results.truncated = true;
            return false;
        },
        results.incomplete,
        stop);
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
    const Workspace& workspace,
    const LanguageDefinition& grammar,
    const CompiledQuery& compiled,
    const QJsonObject& arguments,
    const SearchStop& stop)
{
    const FileScope scope { strings(arguments.value("paths")),
                            strings(arguments.value("include")),
                            strings(arguments.value("exclude")),
                            grammar.extensions };
    SearchResults results;
    QJsonArray diagnostics;
    const auto report = [&](const QString& path, const QString& message, ScanFailure) {
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
        [&](const QString& path, SelectedFile file) {
            const auto read = file.read([&] { return stop.requested(); });
            if (const auto* errorMessage = std::get_if<QString>(&read)) {
                report(path, *errorMessage, ScanFailure::File);
            }
            const auto* source = std::get_if<QByteArray>(&read);
            if (!source || stop.requested()) {
                return !stop.requested();
            }
            const auto parsed = parseSource(*source, grammar.language, compiled, arguments, false, path, results, stop);
            if (const auto* failure = std::get_if<QString>(&parsed)) {
                report(path, *failure, ScanFailure::File);
            }
            return !results.full;
        },
        report,
        { 10000, [&] { return stop.requested(); } });
    if (!error.isEmpty()) {
        return error;
    }
    QJsonObject output = results.data();
    output.insert("diagnostics", diagnostics);
    return output;
}

} // namespace

Result run(const QString& name, const QJsonObject& arguments, const Workspace& workspace, const SearchStop& stop)
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
    if (stop.requested()) {
        return QStringLiteral("Search interrupted");
    }
    if (arguments.contains("paths")) {
        return searchScope(workspace, *grammar, compiled, arguments, stop);
    }
    QByteArray source;
    if (arguments.contains("path")) {
        auto read = readWorkspaceFile(workspace, arguments.value("path").toString(), [&] { return stop.requested(); });
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
        results,
        stop);
    if (name == "syntax_tree" || std::holds_alternative<QString>(output)) {
        return output;
    }
    return results.data();
}

} // namespace treemcp::SingleFile
