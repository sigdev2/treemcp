#include "Query.hpp"

#include <QByteArray>

#include <algorithm>
#include <span>

namespace treemcp {
namespace {

// The C API supplies predicate steps without source offsets. Record operator positions
// outside strings and comments while checking bounds for the recursive query compiler.
std::optional<QueryDiagnostic> inspectQuery(QStringView source, QList<qsizetype>& operators)
{
    constexpr int MAX_NESTING = 128;
    int depth = 0;
    int fieldPrefixes = 0;
    bool afterOpen = false;
    bool quoted = false;
    bool comment = false;
    const qsizetype nul = source.indexOf('\0');
    if (nul >= 0) {
        return QueryDiagnostic { source.left(nul).toUtf8().size(), QStringLiteral("Unexpected NUL in query") };
    }
    for (qsizetype i = 0; i < source.size(); ++i) {
        const QChar ch = source[i];
        if (comment) {
            comment = ch != '\n';
        } else if (quoted) {
            if (ch == '\\') {
                ++i;
            } else if (ch == '"') {
                quoted = false;
            }
        } else if (ch == ';') {
            comment = true;
        } else if (ch.isSpace()) {
            continue;
        } else {
            if (afterOpen && (ch == '#' || ch == '.')) {
                operators.append(i);
            }
            afterOpen = ch == '(';
            quoted = ch == '"';
            if (ch == '(' || ch == '[') {
                ++depth;
            } else if (ch == ')' || ch == ']') {
                depth = std::max(0, depth - 1);
                if (depth == 0) {
                    fieldPrefixes = 0;
                }
            } else if (ch == ':') {
                ++fieldPrefixes;
            }
            if (depth + fieldPrefixes > MAX_NESTING) {
                return QueryDiagnostic { source.left(i).toUtf8().size(),
                                         QStringLiteral("Query nesting and field prefix limit exceeded") };
            }
        }
    }
    return std::nullopt;
}

QByteArrayView literal(const TSQuery* query, uint32_t id)
{
    uint32_t size = 0;
    const char* data = ts_query_string_value_for_id(query, id, &size);
    return { data, size };
}

QString compileError(TSQueryError error)
{
    switch (error) {
    case TSQueryErrorNodeType:
        return QStringLiteral("Unknown node type");
    case TSQueryErrorField:
        return QStringLiteral("Unknown field");
    case TSQueryErrorCapture:
        return QStringLiteral("Unknown capture");
    case TSQueryErrorCaptureLimit:
        return QStringLiteral("A query step supports at most three captures");
    case TSQueryErrorStructure:
        return QStringLiteral("Invalid pattern structure");
    case TSQueryErrorLanguage:
        return QStringLiteral("Incompatible grammar");
    case TSQueryErrorSyntax:
    case TSQueryErrorNone:
        return QStringLiteral("Invalid query syntax");
    }
    return QStringLiteral("Query compilation failed");
}

QString readPredicate(
    const TSQuery* query,
    uint32_t pattern,
    std::span<const TSQueryPredicateStep> steps,
    QList<Predicate>& output)
{
    const QByteArrayView name = literal(query, steps.front().value_id);
    const bool anyOf = name == "any-of?";
    if (!anyOf && name != "eq?" && name != "not-eq?") {
        return QStringLiteral("Unsupported predicate or directive: %1").arg(QString::fromUtf8(name));
    }
    if (steps.size() < 3 || steps[1].type != TSQueryPredicateStepTypeCapture || (!anyOf && steps.size() != 3)) {
        return QStringLiteral("Expected a capture followed by %1")
            .arg(anyOf ? QStringLiteral("one or more literals") : QStringLiteral("one capture or literal"));
    }
    for (const auto& step : steps.subspan(1)) {
        if (step.type == TSQueryPredicateStepTypeCapture &&
            ts_query_capture_quantifier_for_id(query, pattern, step.value_id) == TSQuantifierZero)
        {
            return QStringLiteral("Predicate capture is not bound by this pattern");
        }
    }
    Predicate predicate { steps[1].value_id, name == "not-eq?", {}, {} };
    for (const auto& step : steps.subspan(2)) {
        if (step.type == TSQueryPredicateStepTypeString) {
            predicate.literals.insert(literal(query, step.value_id));
        } else if (!anyOf) {
            predicate.otherCapture = step.value_id;
        } else {
            return QStringLiteral("any-of? alternatives must be literals");
        }
    }
    output.append(std::move(predicate));
    return {};
}

std::optional<QByteArrayView>
    nextCapture(const TSQueryMatch& match, uint32_t id, uint32_t& index, QByteArrayView source)
{
    while (index < match.capture_count) {
        const TSQueryCapture& capture = match.captures[index++];
        if (capture.index == id) {
            const uint32_t start = ts_node_start_byte(capture.node);
            return source.sliced(start, ts_node_end_byte(capture.node) - start);
        }
    }
    return std::nullopt;
}

} // namespace

bool Predicate::accepts(const TSQueryMatch& match, QByteArrayView source) const
{
    uint32_t left = 0;
    uint32_t right = 0;
    while (const auto value = nextCapture(match, capture, left, source)) {
        if (otherCapture) {
            const auto other = nextCapture(match, *otherCapture, right, source);
            if (!other || ((*value == *other) == negated)) {
                return false;
            }
        } else if (literals.contains(*value) == negated) {
            return false;
        }
    }
    return !otherCapture || !nextCapture(match, *otherCapture, right, source);
}

std::optional<QueryDiagnostic> CompiledQuery::compile(const TSLanguage* language, const QString& source)
{
    predicates.clear();
    query.reset();
    QList<qsizetype> operators;
    if (auto error = inspectQuery(source, operators)) {
        return error;
    }
    uint32_t offset = 0;
    TSQueryError error = TSQueryErrorNone;
    const QByteArray bytes = source.toUtf8();
    query.reset(ts_query_new(language, bytes.constData(), static_cast<uint32_t>(bytes.size()), &offset, &error));
    if (!query) {
        return QueryDiagnostic { offset, compileError(error) };
    }
    const uint32_t patterns = ts_query_pattern_count(query.get());
    if (patterns == 0) {
        return QueryDiagnostic { 0, QStringLiteral("Expected a query pattern") };
    }
    predicates.resize(patterns);
    qsizetype operatorIndex = 0;
    for (uint32_t i = 0; i < patterns; ++i) {
        uint32_t count = 0;
        const auto* steps = ts_query_predicates_for_pattern(query.get(), i, &count);
        uint32_t start = 0;
        for (uint32_t end = 0; end < count; ++end) {
            if (steps[end].type == TSQueryPredicateStepTypeDone) {
                const QString message = readPredicate(query.get(), i, { steps + start, end - start }, predicates[i]);
                if (!message.isEmpty()) {
                    const qsizetype byteOffset = operatorIndex < operators.size()
                        ? QStringView(source).left(operators.at(operatorIndex)).toUtf8().size()
                        : ts_query_start_byte_for_pattern(query.get(), i);
                    return QueryDiagnostic { byteOffset, message };
                }
                ++operatorIndex;
                start = end + 1;
            }
        }
    }
    return std::nullopt;
}

} // namespace treemcp
