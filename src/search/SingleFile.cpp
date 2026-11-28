#include "SingleFile.hpp"

#include "LanguageRegistry.hpp"
#include "Matches.hpp"
#include "Scheduler.hpp"
#include "SearchProfile.hpp"
#include "Workspace.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>

#include <algorithm>
#include <deque>
#include <future>

namespace treemcp::SingleFile {
namespace {

// Up to three serialized copies of an entry fit in the wire budget (structured JSON,
// its compact text, and escaping that text). Leave room for the envelope and request ID.
constexpr qsizetype ENTRY_BUDGET = 240 * 1024;
bool appendEntry(QJsonArray& entries, const QJsonObject& entry, qsizetype& remaining)
{
    const qsizetype bytes = SearchProfile::compactJson(QJsonDocument(entry)).size() + 1;
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

struct MatchEntry {
    QJsonObject json;
    qsizetype bytes;
    bool clipped;
    bool cursorIncomplete;
};

struct SearchResults {
    std::vector<MatchEntry> matches;
    qsizetype remaining = ENTRY_BUDGET;
    bool syntaxErrors = false;
    bool truncated = false;
    bool incomplete = false;
    bool full = false;

    bool append(MatchEntry entry, size_t maxMatches)
    {
        if (matches.size() == maxMatches || entry.bytes > remaining) {
            incomplete |= entry.cursorIncomplete;
            full = truncated = true;
            return false;
        }
        truncated |= entry.clipped;
        remaining -= entry.bytes;
        matches.push_back(std::move(entry));
        return true;
    }

    QJsonObject data() const
    {
        QJsonArray entries;
        for (const auto& match : matches) {
            entries.append(match.json);
        }
        return { { "matches", entries },
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
    const auto maxMatches = static_cast<size_t>(arguments.value("maxMatches").toInt());
    const QString error = visitMatches(
        root,
        source,
        compiled,
        [&](const TSQueryMatch& match, bool cursorIncomplete) {
            if (results.matches.size() == maxMatches) {
                results.full = results.truncated = true;
                return false;
            }
            const SearchProfile::Timer collect(SearchProfile::Phase::Collect);
            const SearchMatch entry = copyMatch(match, compiled.query.get(), source, path, {});
            auto json = entry.json();
            const qsizetype bytes = SearchProfile::compactJson(QJsonDocument(json)).size() + 1;
            return results.append({ std::move(json), bytes, entry.clipped(), cursorIncomplete }, maxMatches);
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

struct FileResult {
    QString path;
    QString diagnostic;
    SearchResults results;
};

Result searchScope(
    const Workspace& workspace,
    const LanguageDefinition& grammar,
    const CompiledQuery& compiled,
    const QJsonObject& arguments,
    const SearchStop& stop,
    Scheduler& scheduler,
    FileJobs* files)
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
    const auto searchFile =
        [&](const QString& path, const SelectedFile& file, SearchResults& into, const SearchStop& observer) {
            if (observer.requested()) {
                return QString {};
            }
            const auto read = file.read([&] { return observer.requested(); });
            if (const auto* problem = std::get_if<QString>(&read)) {
                return *problem;
            }
            if (const auto* source = std::get_if<QByteArray>(&read); source && !observer.requested()) {
                const auto parsed =
                    parseSource(*source, grammar.language, compiled, arguments, false, path, into, observer);
                if (const auto* problem = std::get_if<QString>(&parsed)) {
                    return *problem;
                }
            }
            return QString {};
        };
    const auto searchInOrder = [&](const QString& path, const SelectedFile& file) {
        SearchProfile::count(SearchProfile::Phase::Committed);
        const auto diagnostic = searchFile(path, file, results, stop);
        if (!diagnostic.isEmpty()) {
            report(path, diagnostic, ScanFailure::File);
        }
        return !results.full && !stop.requested();
    };

    QString error;
    if (!files || scheduler.workerCount() == 1) {
        error = scanWorkspace(workspace, scope, searchInOrder, report, { 10000, [&] { return stop.requested(); } });
    } else {
        const auto maxMatches = static_cast<size_t>(arguments.value("maxMatches").toInt());
        // Each slot retains at most one file's 240 KiB entry budget, even if the head is slow.
        const size_t window = maxMatches <= 4 ? 2 : scheduler.workerCount();
        std::atomic_bool abandoned = false;
        const SearchStop fileStop { stop.cancelled, stop.deadline, &abandoned };
        std::deque<std::future<FileResult>> pending;
        const auto cleanup = qScopeGuard([&] {
            if (results.full || stop.requested() || !pending.empty()) {
                abandoned.store(true, std::memory_order_relaxed);
                scheduler.cancelFiles(*files);
            }
            scheduler.joinFiles(*files);
        });
        const auto stopped = [&] { return results.full || stop.requested(); };
        const auto mergeNext = [&] {
            auto& next = pending.front();
            SearchProfile::Timer wait(SearchProfile::Phase::ResultWait);
            if (next.wait_until(stop.deadline) != std::future_status::ready || stop.requested()) {
                return;
            }
            wait.finish();
            auto file = next.get();
            pending.pop_front();
            const SearchProfile::Timer merge(SearchProfile::Phase::Merge);
            SearchProfile::count(SearchProfile::Phase::Committed);
            results.syntaxErrors |= file.results.syntaxErrors;
            for (auto& entry : file.results.matches) {
                if (!results.append(std::move(entry), maxMatches)) {
                    return;
                }
            }
            results.incomplete |= file.results.incomplete;
            results.truncated |= file.results.truncated;
            results.full = file.results.full;
            if (!file.diagnostic.isEmpty()) {
                report(file.path, file.diagnostic, ScanFailure::File);
            }
        };
        const auto drain = [&] {
            while (!pending.empty() && !stopped()) {
                mergeNext();
            }
        };
        const auto stopScan = [&] {
            // A prefix can become complete while the walker is filtering unrelated entries.
            while (!pending.empty() && !stopped() &&
                   pending.front().wait_for(std::chrono::seconds::zero()) == std::future_status::ready)
            {
                mergeNext();
            }
            return stopped();
        };
        error = scanWorkspace(
            workspace,
            scope,
            [&](const QString& path, SelectedFile file) {
                // Use the size observed during the POSIX open only as a scheduling hint.
                constexpr qint64 SMALL_FILE_BYTES = 1024;
                const auto size = file.node.openedSize();
                if (pending.empty() && size >= 0 && size < SMALL_FILE_BYTES &&
                    scheduler.tryRunFileInline(*files, fileStop, [&] { searchInOrder(path, file); }))
                {
                    return !stopped();
                }
                using Task = std::packaged_task<FileResult()>;
                auto task = std::make_shared<Task>([&, path, file = std::move(file)]() mutable {
                    const auto selected = std::move(file);
                    FileResult result { path, {}, {} };
                    result.diagnostic = searchFile(path, selected, result.results, fileStop);
                    return result;
                });
                auto next = task->get_future();
                // A completed prefix may end the scan before capacity for this file returns.
                while (!scheduler.submitFile(
                    *files,
                    fileStop,
                    [task] { (*task)(); },
                    pending.empty()))
                {
                    if (pending.empty() || stopped()) {
                        return false;
                    }
                    mergeNext();
                    if (stopped()) {
                        return false;
                    }
                }
                task.reset(); // Removing a queued job must wake its future even while the scan waits.
                pending.push_back(std::move(next));
                // The queue is in traversal order, regardless of the order in which futures become ready.
                if (pending.size() == window && !stopped()) {
                    mergeNext();
                }
                return !stopScan();
            },
            [&](const QString& path, const QString& message, ScanFailure failure) {
                drain();
                if (!stopped()) {
                    report(path, message, failure);
                }
            },
            { 10000, stopScan });
        drain();
    }
    if (!error.isEmpty()) {
        return error;
    }
    QJsonObject output = results.data();
    output.insert("diagnostics", diagnostics);
    return output;
}

} // namespace

Result
    run(const QString& name,
        const QJsonObject& arguments,
        const Workspace& workspace,
        const SearchStop& stop,
        Scheduler& scheduler,
        FileJobs* files)
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
        return searchScope(workspace, *grammar, compiled, arguments, stop, scheduler, files);
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
