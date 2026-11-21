#ifndef TREEMCP_QUERY_HPP
#define TREEMCP_QUERY_HPP

#include "SearchStop.hpp"

#include <QByteArrayView>
#include <QList>
#include <QSet>
#include <QString>
#include <tree_sitter/api.h>

#include <memory>
#include <optional>

namespace treemcp {

using Parser = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
using Tree = std::unique_ptr<TSTree, decltype(&ts_tree_delete)>;
using Query = std::unique_ptr<TSQuery, decltype(&ts_query_delete)>;
using Cursor = std::unique_ptr<TSQueryCursor, decltype(&ts_query_cursor_delete)>;

struct QueryDiagnostic {
    qsizetype byteOffset;
    QString message;
};

struct Predicate {
    uint32_t capture;
    bool negated;
    std::optional<uint32_t> otherCapture;
    QSet<QByteArrayView> literals;

    bool accepts(const TSQueryMatch& match, QByteArrayView source, const SearchStop& stop) const;
};

struct CompiledQuery {
    Query query { nullptr, ts_query_delete };
    QList<QList<Predicate>> predicates;
    bool hasRepeatedCapture = false;
    std::optional<QueryDiagnostic> compile(const TSLanguage* language, const QString& source);
};

} // namespace treemcp

#endif // TREEMCP_QUERY_HPP
