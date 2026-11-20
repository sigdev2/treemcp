#include "BatchSearch.hpp"

#include "LanguageRegistry.hpp"
#include "Matches.hpp"
#include "Tools.hpp"
#include "Workspace.hpp"
#include "WorkspaceFilter.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThreadPool>

#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>

namespace treemcp {
namespace {

auto firstRange(const SearchMatch& match)
{
    if (match.captures.empty()) {
        return std::pair<uint32_t, uint32_t> {};
    }
    return std::pair { match.captures.front().start, match.captures.front().end };
}

struct MatchOrder {
    bool operator()(const SearchMatch& left, const SearchMatch& right) const
    {
        const auto lhs = std::tuple { left.pathBytes, firstRange(left), left.pattern };
        const auto rhs = std::tuple { right.pathBytes, firstRange(right), right.pattern };
        if (lhs != rhs) {
            return lhs < rhs;
        }
        const auto captureOrder = [](const MatchCapture& a, const MatchCapture& b) {
            return std::tie(a.start, a.end, a.name) < std::tie(b.start, b.end, b.name);
        };
        const auto& a = left.captures;
        const auto& b = right.captures;
        if (std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), captureOrder)) {
            return true;
        }
        return left.capturesTruncated < right.capturesTruncated &&
            !std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end(), captureOrder);
    }
};

struct Item {
    QString id;
    CompiledQuery compiled;
    QJsonObject error;
    std::multiset<SearchMatch, MatchOrder> best;
    int limit = 0;
    bool truncated = false;
    bool incomplete = false;
    bool syntaxErrors = false;
};

QString clipped(const QString& string)
{
    QString value = string.left(256);
    if (!value.isEmpty() && value.back().isHighSurrogate()) {
        value.chop(1);
    }
    return value;
}

struct Batch {
    std::vector<Item> items;
    // The item vector is sized before compilation and never resized.
    std::map<const LanguageDefinition*, std::vector<Item*>> groups;
    const int maxMatches;
    const SearchStop stop;
    std::mutex mutex;
    size_t retained = 0;
    int visited = 0;
    int read = 0;
    int parsed = 0;
    int builds = 0;
    int executions = 0;
    bool scopeIncomplete = false;
    std::set<std::pair<QString, QString>> diagnostics;

    Batch(size_t count, int limit, SearchStop observer) :
            items(count),
            maxMatches(limit),
            stop(observer)
    {
    }

    // Called under mutex. The global prefix always gives earlier items first claim on the budget.
    void collect(Item& item, SearchMatch match)
    {
        item.best.insert(std::move(match));
        ++retained;
        Item* overflow = nullptr;
        if (item.best.size() > static_cast<size_t>(item.limit)) {
            overflow = &item;
        } else if (retained > static_cast<size_t>(maxMatches)) {
            overflow = &*std::find_if(items.rbegin(), items.rend(), [](const Item& candidate) {
                return !candidate.best.empty();
            });
        }
        if (overflow) {
            overflow->best.erase(std::prev(overflow->best.end()));
            overflow->truncated = true;
            --retained;
        }
    }

    void diagnostic(const QString& path, const QString& message)
    {
        diagnostics.emplace(clipped(path), clipped(message));
        if (diagnostics.size() > 16) {
            diagnostics.erase(std::prev(diagnostics.end()));
        }
    }

    void searchFile(const QString& path, const QByteArray& source)
    {
        const QByteArray pathBytes = path.toUtf8();
        bool countedFile = false;
        for (const auto& [grammar, queries] : groups) {
            if (stop.requested()) {
                return;
            }
            if (std::none_of(grammar->extensions.begin(), grammar->extensions.end(), [&](const QString& suffix) {
                    return path.endsWith(suffix);
                }))
            {
                continue;
            }
            const Tree tree = parseTree(source, grammar->language, &stop);
            if (!tree) {
                if (!stop.requested()) {
                    std::lock_guard lock(mutex);
                    diagnostic(path, QStringLiteral("Cannot parse source"));
                    for (auto* item : queries) {
                        item->incomplete = true;
                    }
                }
                continue;
            }
            {
                std::lock_guard lock(mutex);
                ++builds;
                if (!countedFile) {
                    ++parsed;
                    countedFile = true;
                }
            }
            for (auto* item : queries) {
                if (stop.requested()) {
                    return;
                }
                const TSNode root = ts_tree_root_node(tree.get());
                bool incomplete = false;
                const auto error = visitMatches(
                    root,
                    source,
                    item->compiled,
                    [&](const TSQueryMatch& match) {
                        auto entry = copyMatch(match, item->compiled.query.get(), source, path, pathBytes);
                        std::lock_guard lock(mutex);
                        collect(*item, std::move(entry));
                        return true;
                    },
                    incomplete,
                    &stop);
                std::lock_guard lock(mutex);
                ++executions;
                item->syntaxErrors |= ts_node_has_error(root);
                item->incomplete |= incomplete || !error.isEmpty();
                if (!error.isEmpty()) {
                    diagnostic(path, error);
                }
            }
        }
    }

    QJsonObject metadata() const
    {
        QJsonArray entries;
        for (const auto& item : items) {
            QJsonObject entry { { "id", item.id },
                                { "matches", QJsonArray {} },
                                { "incomplete", item.incomplete },
                                { "truncated", item.truncated },
                                { "hasSyntaxErrors", item.syntaxErrors } };
            if (!item.error.isEmpty()) {
                entry.insert("error", item.error);
            }
            entries.append(entry);
        }
        QJsonArray messages;
        for (const auto& [path, message] : diagnostics) {
            messages.append(QJsonObject { { "path", path }, { "message", message } });
        }
        return { { "items", entries },
                 { "diagnostics", messages },
                 { "truncated", false },
                 { "incomplete", false },
                 { "deadlineExceeded", false },
                 { "stats",
                   QJsonObject { { "filesVisited", visited },
                                 { "filesRead", read },
                                 { "filesParsed", parsed },
                                 { "treeBuilds", builds },
                                 { "queryExecutions", executions } } } };
    }

    QJsonObject finish(const QJsonValue& id, int byteLimit)
    {
        QJsonObject data = metadata();
        // Charge the empty envelope (including LF), then each entry in both structured and escaped text content.
        qsizetype remaining =
            byteLimit - QJsonDocument(Tools::dataReply(id, data)).toJson(QJsonDocument::Compact).size() - 1;
        QJsonArray entries = data.value("items").toArray();
        // A traversal deadline leaves a bounded prefix ready to publish. If output itself
        // crosses the deadline, finish with the entries already serialized.
        const bool expiredBeforeOutput = stop.requested();
        bool full = false;
        bool truncated = false;
        bool incomplete = false;
        for (qsizetype i = 0; i < entries.size(); ++i) {
            auto& item = items[static_cast<size_t>(i)];
            QJsonArray matches;
            for (const auto& match : item.best) {
                if (stop.cancelled.load(std::memory_order_relaxed) || (!expiredBeforeOutput && stop.requested())) {
                    break;
                }
                if (!full) {
                    const auto entry = match.json();
                    const auto bytes = QJsonDocument(entry).toJson(QJsonDocument::Compact);
                    const QJsonArray quoted { QString::fromUtf8(bytes) };
                    const qsizetype cost = bytes.size() + QJsonDocument(quoted).toJson(QJsonDocument::Compact).size() -
                        4 + (matches.isEmpty() ? 0 : 2);
                    if (cost <= remaining) {
                        remaining -= cost;
                        matches.append(entry);
                        item.truncated |= match.clipped();
                        continue;
                    }
                    full = true;
                }
                item.truncated = true;
                break;
            }
            auto entry = entries.at(i).toObject();
            entry.insert("matches", matches);
            entry.insert("truncated", item.truncated);
            entries[i] = entry;
            truncated |= item.truncated;
        }
        const bool expired = stop.requested();
        for (qsizetype i = 0; i < entries.size(); ++i) {
            auto entry = entries.at(i).toObject();
            const bool partial = scopeIncomplete || expired || entry.value("incomplete").toBool();
            entry.insert("incomplete", partial);
            entries[i] = entry;
            incomplete |= partial;
        }
        data.insert("items", entries);
        data.insert("truncated", truncated);
        data.insert("incomplete", incomplete);
        data.insert("deadlineExceeded", expired);
        return data;
    }
};

QStringList strings(const QJsonValue& value)
{
    QStringList list;
    for (const auto entry : value.toArray()) {
        list.append(entry.toString());
    }
    return list;
}

} // namespace

QString validateBatch(const QJsonObject& arguments)
{
    const auto queries = arguments.value("queries").toArray();
    qsizetype bytes = 0;
    QSet<QString> ids;
    for (const auto query : queries) {
        const auto object = query.toObject();
        const auto itemId = object.value("id").toString();
        if (ids.contains(itemId)) {
            return QStringLiteral("Duplicate batch query ID");
        }
        ids.insert(itemId);
        bytes += object.value("query").toString().toUtf8().size();
    }
    if (bytes > 256 * 1024) {
        return QStringLiteral("Aggregate query text exceeds 256 KiB");
    }

    for (const auto& path : strings(arguments.value("paths"))) {
        if (!validSelector(path)) {
            return QStringLiteral("Expected a relative workspace path");
        }
    }
    for (const auto& pattern : strings(arguments.value("include")) + strings(arguments.value("exclude"))) {
        if (!WorkspaceGlob::compile(pattern)) {
            return QStringLiteral("Invalid workspace glob");
        }
    }
    return {};
}

SingleFile::Result batchSearch(
    const QJsonValue& id,
    const QJsonObject& arguments,
    const WorkspaceNode& workspace,
    const BatchControl& control)
{
    const auto queries = arguments.value("queries").toArray();
    Batch batch(
        static_cast<size_t>(queries.size()),
        arguments.value("maxTotalMatches").toInt(),
        { control.cancelled, control.received + std::chrono::milliseconds(arguments.value("deadlineMs").toInt()) });
    FileScope scope { strings(arguments.value("paths")),
                      strings(arguments.value("include")),
                      strings(arguments.value("exclude")),
                      {} };
    for (qsizetype i = 0; i < queries.size(); ++i) {
        const auto object = queries.at(i).toObject();
        auto& item = batch.items[static_cast<size_t>(i)];
        item.id = object.value("id").toString();
        item.limit = object.value("maxMatches").toInt();
        const auto* grammar = LanguageRegistry::getLanguage(object.value("language").toString());
        if (!grammar) {
            item.error.insert("message", QStringLiteral("Unknown language"));
        } else if (!batch.stop.requested()) {
            if (const auto error = item.compiled.compile(grammar->language, object.value("query").toString())) {
                item.error = { { "byteOffset", error->byteOffset }, { "message", clipped(error->message) } };
            } else {
                batch.groups[grammar].push_back(&item);
                scope.extensions.append(grammar->extensions);
            }
        }
        item.incomplete = !item.error.isEmpty() || batch.stop.requested();
    }
    scope.extensions.removeDuplicates();
    QThreadPool pool;
    pool.setMaxThreadCount(control.workers);
    QSemaphore capacity(control.workers);
    const ScanOptions limits { arguments.value("maxFiles").toInt(),
                               [&]() { return scope.extensions.isEmpty() || batch.stop.requested(); },
                               &batch.visited,
                               &batch.read };
    const auto error = scanWorkspace(
        workspace,
        scope,
        [&](const QString& path, const QByteArray& source) {
            capacity.acquire();
            if (batch.stop.requested()) {
                capacity.release();
                return false;
            }
            pool.start([&batch, &capacity, path, source]() {
                const auto release = qScopeGuard([&capacity]() { capacity.release(); });
                batch.searchFile(path, source);
            });
            return true;
        },
        [&](const QString& path, const QString& message) {
            std::lock_guard lock(batch.mutex);
            batch.scopeIncomplete = true;
            batch.diagnostic(path, message);
        },
        limits);
    pool.waitForDone();
    if (!error.isEmpty()) {
        return error;
    }
    return batch.finish(id, arguments.value("maxResponseBytes").toInt());
}

} // namespace treemcp
