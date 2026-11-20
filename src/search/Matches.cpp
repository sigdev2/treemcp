#include "Matches.hpp"

#include <QJsonArray>
#include <QStringDecoder>

#include <algorithm>
#include <limits>

namespace treemcp {
namespace {

QJsonObject range(uint32_t start, uint32_t end, TSPoint first, TSPoint last)
{
    return { { "startByte", qint64 { start } },    { "endByte", qint64 { end } },
             { "startRow", qint64 { first.row } }, { "startColumn", qint64 { first.column } },
             { "endRow", qint64 { last.row } },    { "endColumn", qint64 { last.column } } };
}

} // namespace

QJsonObject nodeRange(TSNode node)
{
    return range(ts_node_start_byte(node), ts_node_end_byte(node), ts_node_start_point(node), ts_node_end_point(node));
}

bool SearchStop::requested() const
{
    return cancelled.load(std::memory_order_relaxed) || std::chrono::steady_clock::now() >= deadline;
}

Tree parseTree(const QByteArray& source, const TSLanguage* language, const SearchStop* stop)
{
    const Parser parser { ts_parser_new(), ts_parser_delete };
    Tree tree { nullptr, ts_tree_delete };
    if (!parser || !ts_parser_set_language(parser.get(), language)) {
        return tree;
    }
    QByteArrayView bytes(source);
    const TSInput input { &bytes,
                          [](void* payload, uint32_t offset, TSPoint, uint32_t* count) -> const char* {
                              const auto view = *static_cast<QByteArrayView*>(payload);
                              if (offset >= view.size()) {
                                  *count = 0;
                                  return nullptr;
                              }
                              *count = static_cast<uint32_t>(view.size() - offset);
                              return view.data() + offset;
                          },
                          TSInputEncodingUTF8,
                          nullptr };
    const TSParseOptions options { &stop, [](TSParseState* state) {
                                      const auto* observer = *static_cast<const SearchStop**>(state->payload);
                                      return observer && observer->requested();
                                  } };
    tree.reset(
        stop ? ts_parser_parse_with_options(parser.get(), nullptr, input, options)
             : ts_parser_parse(parser.get(), nullptr, input));
    return tree;
}

SearchMatch copyMatch(
    const TSQueryMatch& match,
    const TSQuery* query,
    QByteArrayView source,
    const QString& path,
    const QByteArray& pathBytes)
{
    constexpr uint32_t MAX_CAPTURES = 64;
    constexpr uint32_t MAX_TEXT_BYTES = 1024;
    SearchMatch entry { path, pathBytes, match.pattern_index, match.capture_count > MAX_CAPTURES, {} };
    const auto count = std::min<uint32_t>(match.capture_count, MAX_CAPTURES);
    entry.captures.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& capture = match.captures[i];
        const auto node = capture.node;
        const auto start = ts_node_start_byte(node);
        const auto end = ts_node_end_byte(node);
        const auto size = end - start;
        auto length = std::min(size, MAX_TEXT_BYTES);
        while (length < size && (static_cast<unsigned char>(source[start + length]) & 0xc0U) == 0x80U) {
            --length;
        }
        uint32_t nameSize = 0;
        const char* name = ts_query_capture_name_for_id(query, capture.index, &nameSize);
        entry.captures.push_back({ { name, nameSize },
                                   start,
                                   end,
                                   ts_node_start_point(node),
                                   ts_node_end_point(node),
                                   QByteArray(source.data() + start, length) });
    }
    return entry;
}

bool SearchMatch::clipped() const
{
    return capturesTruncated || std::any_of(captures.begin(), captures.end(), [](const MatchCapture& capture) {
               return capture.text.size() < capture.end - capture.start;
           });
}

QJsonObject SearchMatch::json() const
{
    QJsonArray entries;
    for (const auto& capture : captures) {
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::ConvertInitialBom);
        const QString text = decoder.decode(capture.text);
        entries.append(
            QJsonObject { { "name", QString::fromUtf8(capture.name) },
                          { "range", range(capture.start, capture.end, capture.startPoint, capture.endPoint) },
                          { "text", text },
                          { "textTruncated", capture.text.size() < capture.end - capture.start } });
    }
    QJsonObject entry { { "patternIndex", pattern },
                        { "captures", entries },
                        { "capturesTruncated", capturesTruncated } };
    if (!path.isEmpty()) {
        entry.insert("path", path);
    }
    return entry;
}

QString visitMatches(
    TSNode root,
    QByteArrayView source,
    const CompiledQuery& compiled,
    const std::function<bool(const TSQueryMatch&)>& visit,
    bool& incomplete,
    const SearchStop* stop)
{
    // The pinned runtime records up to three captures per visited node, but the C API
    // exposes only a 16-bit capture count. Guard repeated captures before it can wrap.
    constexpr uint32_t MAX_REPEAT_TREE_NODES = std::numeric_limits<uint16_t>::max() / 3;
    if (ts_node_descendant_count(root) > MAX_REPEAT_TREE_NODES) {
        const TSQuery* query = compiled.query.get();
        for (uint32_t pattern = 0; pattern < ts_query_pattern_count(query); ++pattern) {
            if (stop && stop->requested()) {
                incomplete = true;
                return {};
            }
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
    TSQueryCursorOptions options { nullptr, [](TSQueryCursorState* state) {
                                      return (*static_cast<const SearchStop**>(state->payload))->requested();
                                  } };
    if (stop) {
        options.payload = &stop;
        ts_query_cursor_exec_with_options(cursor.get(), compiled.query.get(), root, &options);
    } else {
        ts_query_cursor_exec(cursor.get(), compiled.query.get(), root);
    }
    TSQueryMatch match {};
    while (ts_query_cursor_next_match(cursor.get(), &match)) {
        if (stop && stop->requested()) {
            break;
        }
        const auto& predicates = compiled.predicates.at(match.pattern_index);
        if (!std::all_of(predicates.begin(), predicates.end(), [&](const Predicate& p) {
                return p.accepts(match, source);
            }))
        {
            continue;
        }
        if (!visit(match)) {
            break;
        }
    }
    incomplete |= ts_query_cursor_did_exceed_match_limit(cursor.get()) || (stop && stop->requested());
    return {};
}

} // namespace treemcp
