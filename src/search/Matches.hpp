#ifndef TREEMCP_MATCHES_HPP
#define TREEMCP_MATCHES_HPP

#include "Query.hpp"

#include <QJsonObject>

#include <atomic>
#include <chrono>
#include <functional>
#include <vector>

namespace treemcp {

struct SearchStop {
    const std::atomic_bool& cancelled;
    std::chrono::steady_clock::time_point deadline;
    bool requested() const;
};

Tree parseTree(const QByteArray& source, const TSLanguage* language, const SearchStop* stop = nullptr);

struct MatchCapture {
    QByteArrayView name;
    uint32_t start;
    uint32_t end;
    TSPoint startPoint;
    TSPoint endPoint;
    QByteArray text;
};

struct SearchMatch {
    QString path;
    QByteArray pathBytes;
    uint16_t pattern;
    bool capturesTruncated;
    std::vector<MatchCapture> captures;

    QJsonObject json() const;
    bool clipped() const;
};

// Capture names refer to the compiled query. All source-derived data is copied and bounded.
SearchMatch copyMatch(
    const TSQueryMatch& match,
    const TSQuery* query,
    QByteArrayView source,
    const QString& path,
    const QByteArray& pathBytes);
QJsonObject nodeRange(TSNode node);
QString visitMatches(
    TSNode root,
    QByteArrayView source,
    const CompiledQuery& compiled,
    const std::function<bool(const TSQueryMatch&)>& visit,
    bool& incomplete,
    const SearchStop* stop = nullptr);

} // namespace treemcp
#endif // TREEMCP_MATCHES_HPP
