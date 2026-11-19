#include "CheckSearch.hpp"

#include <QJsonArray>

#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void checkValidation(const ToolCall& call)
{
    const auto validate = [&](const QString& query) {
        return call("query_validate", { { "language", "python" }, { "query", query } });
    };
    const QString prefix = QStringLiteral("; \u03c0\n(identifier) @n\n");
    const auto good = validate(prefix + "(integer) @n");
    require(good.value("valid") == true && good.value("patternCount") == 2, "Expected two valid patterns");
    auto error = validate(prefix + "(unknown_node)").value("diagnostic").toObject();
    require(
        validate(prefix + "((integer) @v (#eq? @n @v))").value("valid") == false,
        "Accepted a predicate capture bound only by a different pattern");
    require(error.value("byteOffset") == prefix.toUtf8().size() + 1, "Incorrect compiler byte offset");

    const QString predicates = prefix + QStringLiteral("((identifier) @x (#eq? @x \"(#ignored?)\") ; (#fake!)\n ");
    for (const QString& bad : QStringList { "(#match? @x \"a\")", "(#set! k v)", "(#eq? @x)", "(#any-of? @x @x)" }) {
        const auto data = validate(predicates + bad + ')');
        error = data.value("diagnostic").toObject();
        require(
            data.value("valid") == false && error.value("byteOffset") == predicates.toUtf8().size() + 1,
            "Predicate error must point to the invalid operator");
    }
    require(validate(QString(32769, QChar(0xe9))).value("isError") == true, "Query byte budget ignored");
    require(validate(QString(65524, ' ') + "(identifier)").value("valid") == true, "Query at byte limit rejected");
    require(
        validate(QString(129, '(') + "(identifier)" + QString(129, ')')).value("valid") == false,
        "Query nesting limit ignored");
    require(
        call("query_validate", { { "language", "rust" }, { "query", "(_)" } }).value("isError") == true,
        "Unknown language accepted");
}

void checkTree(const ToolCall& call)
{
    QJsonObject args { { "language", "cpp" }, { "source", "int main() { return 1; }\n" }, { "maxDepth", 0 } };
    auto tree = call("syntax_tree", args);
    const auto nodes = tree.value("nodes").toArray();
    require(
        nodes.size() == 1 && nodes.first().toObject().value("type") == "translation_unit" &&
            tree.value("truncated") == true && tree.value("hasSyntaxErrors") == false,
        "Incorrect root or depth bound");
    args.insert("maxDepth", 64);
    args.insert("maxNodes", 2);
    tree = call("syntax_tree", args);
    require(tree.value("nodes").toArray().size() == 2 && tree.value("truncated") == true, "Tree node limit ignored");
    args.insert("source", "");
    args.insert("maxNodes", 1);
    require(call("syntax_tree", args).value("truncated") == false, "Exact tree limit reported as truncated");
    args.insert("source", "int {");
    require(call("syntax_tree", args).value("hasSyntaxErrors") == true, "Missing syntax error diagnostic");
    args.insert("maxNodes", 1.5);
    require(call("syntax_tree", args).value("isError") == true, "Fractional limit accepted");
    args.remove("maxNodes");
    args.insert("source", QString(262145, QChar(0xe9)));
    require(call("syntax_tree", args).value("isError") == true, "Source byte limit ignored");
    require(
        call("syntax_tree", { { "language", "cpp" }, { "path", "example.cpp" }, { "source", "" } }).value("isError") ==
            true,
        "Conflicting source and path accepted");
}

void checkMatches(const ToolCall& call)
{
    QJsonObject args { { "language", "python" },
                       { "source", QStringLiteral("# \u03c0\r\n\u03c0 = caf\u00e9(\u03c0)\n") },
                       { "query", QStringLiteral("((identifier) @name (#any-of? @name \"x\" \"caf\u00e9\"))") },
                       { "maxMatches", 1 } };
    auto result = call("structural_search", args);
    require(
        result.value("matches").toArray().size() == 1 && result.value("truncated") == false &&
            result.value("incomplete") == false,
        "Expected exactly one complete match");
    const auto match = result.value("matches").toArray().first().toObject();
    const auto capture = match.value("captures").toArray().first().toObject();
    const QJsonObject expected { { "startByte", 11 }, { "endByte", 16 },    { "startRow", 1 },
                                 { "endRow", 1 },     { "startColumn", 5 }, { "endColumn", 10 } };
    require(
        match.value("patternIndex") == 0 && capture.value("range") == expected && capture.value("name") == "name" &&
            capture.value("text") == QStringLiteral("caf\u00e9"),
        "Incorrect UTF-8 capture or range");
    args.insert("query", "((identifier) @id (#eq? @id \"absent\"))");
    result = call("structural_search", args);
    require(result.value("matches") == QJsonArray {}&& result.value("truncated") == false, "Incorrect empty result");
    args.insert("query", "(identifier) @id");
    require(call("structural_search", args).value("truncated") == true, "Match limit ignored");
    args.insert("source", QString(1023, 'a') + QChar(0xe9));
    result = call("structural_search", args);
    const auto clipped =
        result.value("matches").toArray().first().toObject().value("captures").toArray().first().toObject();
    require(
        clipped.value("textTruncated") == true && clipped.value("text") == QString(1023, 'a') &&
            clipped.value("range").toObject().value("endByte") == 1025 && result.value("truncated") == true,
        "Clipped capture lost its UTF-8 boundary or full range");
    args.insert("source", QStringLiteral("name\n").repeated(70));
    args.insert("query", "(module (expression_statement (identifier) @name)+)");
    result = call("structural_search", args);
    const auto repeated = result.value("matches").toArray().first().toObject();
    require(
        repeated.value("capturesTruncated") == true && repeated.value("captures").toArray().size() == 64,
        "Capture count limit ignored");
    args.insert("query", "((module (expression_statement (identifier) @name)+) (#not-eq? @name \"last\"))");
    args.insert("source", args.value("source").toString() + "last\n");
    require(
        call("structural_search", args).value("matches") == QJsonArray {},
        "Predicate evaluated on displayed captures only");
    args.insert("source", QStringLiteral("x\n").repeated(11000));
    require(call("structural_search", args).value("isError") == true, "Repeated capture count can overflow");
    args.insert("language", "cpp");
    args.insert("source", "int f(int a,int b){return (a+a)*(a+b);}");
    const QString binary = QStringLiteral("((binary_expression left: (identifier) @a right: (identifier) @b) ");
    args.insert("query", binary + "(#eq? @a @b))\n" + binary + "(#not-eq? @a @b))");
    args.insert("maxMatches", 2);
    const auto pairs = call("structural_search", args).value("matches").toArray();
    require(
        pairs.size() == 2 && pairs.first().toObject().value("patternIndex") == 0 &&
            pairs.last().toObject().value("patternIndex") == 1,
        "Capture equality or inequality failed");
    args.insert("query", "((identifier) @a (#unknown? @a))");
    require(call("structural_search", args).value("isError") == true, "Invalid search query must fail");
}

void checkResponseBudget(const ToolCall& call)
{
    const QString line = '"' + QStringLiteral("\\\"").repeated(380) + QStringLiteral("\"\n");
    const QJsonObject args { { "language", "python" },
                             { "source", line.repeated(350) },
                             { "query", "(string) @quoted" },
                             { "maxMatches", 1000 } };
    const auto result = call("structural_search", args);
    const auto count = result.value("matches").toArray().size();
    require(
        count > 0 && count < 350 && result.value("truncated") == true && result.value("incomplete") == false,
        "Expected partial matches within the wire budget");
}

void checkCursorLimit(const ToolCall& call)
{
    const QJsonObject args { { "language", "python" },
                             { "source", "x" },
                             { "query", QStringLiteral("(identifier) @id\n").repeated(300) },
                             { "maxMatches", 1000 } };
    const auto result = call("structural_search", args);
    const auto count = result.value("matches").toArray().size();
    require(
        result.value("incomplete") == true && result.value("truncated") == false && count > 0 && count < 300,
        "A dropped query state must mark execution incomplete");
}

} // namespace

void checkSearch(const ToolCall& call)
{
    checkValidation(call);
    checkTree(call);
    checkMatches(call);
    checkResponseBudget(call);
    checkCursorLimit(call);
}
