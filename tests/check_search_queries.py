"""Check Tree-sitter query results against explicit expectations through MCP stdio."""

from collections import Counter
import json
from pathlib import Path
import re
import sys
import tempfile

from check_workspace import session, write


def hit(*captures, pattern=0):
    return pattern, captures


PYTHON_SOURCE = """\
from api import get, post
from api import get, delete
from api import delete, post

@trace
async def fetch(client, key):
    return await client.get(key, timeout=5)

@trace
async def typed(client, key) -> bytes:
    return await client.post(key, timeout=10)

@trace
def sync(client, key):
    return client.get(key, timeout=20)

async def other(client, key):
    await cache.get(key, timeout=30)
    await client.put(key, timeout=40)
    await client.get(key, retries=50)

names = [item.name for item in records if item.ready]
all_names = [item.name for item in records]
stream = (item.name for item in records if item.ready)
"""

PYTHON_QUERIES = [
    ("approved-imports", """
        ((import_from_statement module_name: (dotted_name) @module
           . name: (dotted_name) @member . name: (dotted_name) @member .)
          (#any-of? @member "get" "post"))
     """, [hit(("module", "api"), ("member", "get"), ("member", "post"))]),
    ("unannotated-async", """
        (decorated_definition
          (decorator (identifier) @decorator)
          definition: (function_definition "async" name: (identifier) @fn !return_type))
     """, [hit(("decorator", "trace"), ("fn", "fetch"))]),
    ("awaited-timeouts", """
        ((await (call
          function: (attribute object: (identifier) @receiver attribute: (identifier) @method)
          arguments: (argument_list (keyword_argument name: (identifier) @key value: (_) @value))))
          (#eq? @receiver "client")
          (#any-of? @method "get" "post")
          (#eq? @key "timeout"))
     """, [hit(("receiver", "client"), ("method", "get"), ("key", "timeout"), ("value", "5")),
           hit(("receiver", "client"), ("method", "post"), ("key", "timeout"), ("value", "10"))]),
    ("no-captures", "(decorator)", [hit(), hit(), hit()]),
    ("filtered-comprehension", """
        (list_comprehension
          body: (attribute attribute: (identifier) @field)
          (for_in_clause left: (identifier) @target right: (_) @collection)
          (if_clause (_) @condition))
     """, [hit(("field", "name"), ("target", "item"), ("collection", "records"), ("condition", "item.ready"))]),
]


CPP_SOURCE = """\
#include <vector>
#include "service.hpp"
int transform(int);
int transform(int x) { return x; }
void run() {
    io::read<int>(src);
    io::read(src);
    read<int>(src);
    client.read(1);
    client->read(2);
    client.write(3);
    client.read(4, 5);
    client.read(flag);
    read(6);
}
"""

CPP_QUERIES = [
    ("local-include", "(preproc_include path: (string_literal (string_content) @header))",
     [hit(("header", "service.hpp"))]),
    ("functions", """
        (declaration declarator: (function_declarator declarator: (identifier) @decl))
        (function_definition declarator: (function_declarator declarator: (identifier) @def))
     """, [hit(("decl", "transform")), hit(("def", "transform"), pattern=1), hit(("def", "run"), pattern=1)]),
    ("qualified-template", """
        (call_expression function: (qualified_identifier
          scope: (namespace_identifier) @ns
          name: (template_function name: (identifier) @fn
            arguments: (template_argument_list (type_descriptor) @type))))
     """, [hit(("ns", "io"), ("fn", "read"), ("type", "int"))]),
    ("member-call", """
        ((call_expression function: (field_expression
          argument: (identifier) @object operator: "." field: (field_identifier) @method)
          arguments: (argument_list . [(number_literal) (identifier)] @arg .))
          (#not-eq? @method "write"))
     """, [hit(("object", "client"), ("method", "read"), ("arg", "1")),
           hit(("object", "client"), ("method", "read"), ("arg", "flag"))]),
]


PAIR_SOURCE = """\
same(a, b) == pair(a, b)
swapped(a, b) == pair(b, a)
mixed(a, b) == pair(a, c)
short_equal(a) == pair(a, b)
long_equal(a, b) == pair(a)
short_different(x) == pair(a, b)
long_different(x, y) == pair(a)
"""

PAIR_PATTERN = """
    (comparison_operator
      (call function: (identifier) @case
            arguments: (argument_list . (identifier) @lhs . (identifier)? @lhs .))
      (call arguments: (argument_list . (identifier) @rhs . (identifier)? @rhs .)))
"""

PAIR_QUERIES = [
    ("pairwise-equal", f"({PAIR_PATTERN} (#eq? @lhs @rhs))",
     [hit(("case", "same"), ("lhs", "a"), ("lhs", "b"), ("rhs", "a"), ("rhs", "b"))]),
    ("pairwise-different", f"({PAIR_PATTERN} (#not-eq? @lhs @rhs))",
     [hit(("case", "swapped"), ("lhs", "a"), ("lhs", "b"), ("rhs", "b"), ("rhs", "a"))]),
]


def check_matches(result, expected, source, path, syntax_errors):
    assert "isError" not in result, result
    assert result["hasSyntaxErrors"] == syntax_errors, result
    assert not result["incomplete"] and not result["truncated"], result
    raw = source.encode("utf-8")
    actual = []
    for match in result["matches"]:
        assert match.get("path") == path and not match["capturesTruncated"], match
        captures = []
        for capture in match["captures"]:
            span = capture["range"]
            start, end = span["startByte"], span["endByte"]
            assert 0 <= start <= end <= len(raw), span
            assert not capture["textTruncated"] and raw[start:end].decode("utf-8") == capture["text"], capture
            for key, offset in [("start", start), ("end", end)]:
                assert span[key + "Row"] == raw.count(b"\n", 0, offset), span
                assert span[key + "Column"] == offset - raw.rfind(b"\n", 0, offset) - 1, span
            captures.append((capture["name"], capture["text"]))
        actual.append(hit(*captures, pattern=match["patternIndex"]))
    # Cursor order and batch sort order can differ. Capture order and multiplicity matter.
    assert Counter(actual) == Counter(expected), (actual, expected)


def check_tree(call, root):
    # The missing semicolon is an anonymous, zero-width child after the identifier.
    source = "int x"
    path = "declaration.txt"
    write(root, path, source)
    expected = [("translation_unit", 0, True, False, 0, 5), ("declaration", 1, True, False, 0, 5),
                ("primitive_type", 2, True, False, 0, 3), ("identifier", 2, True, False, 4, 5),
                (";", 2, False, True, 5, 5)]
    for selector in [dict(source=source), dict(path=path)]:
        tree = call("syntax_tree", language="cpp", maxDepth=2, maxNodes=5, **selector)
        assert tree["hasSyntaxErrors"] and not tree["truncated"], tree
        assert len(tree["nodes"]) == len(expected), tree
        for node, (kind, depth, named, missing, start, end) in zip(tree["nodes"], expected):
            assert node == dict(type=kind, depth=depth, named=named, missing=missing,
                                range=dict(startByte=start, endByte=end, startRow=0, endRow=0,
                                           startColumn=start, endColumn=end)), node


def check_query_diagnostics(call):
    escaped = r'((identifier) @x (#eq? @x "a\"b") '
    cases = [("(identifier", 11, "Invalid query syntax"),
             ("(identifier (identifier))", 12, "Invalid pattern structure"),
             ('((identifier) @x (#eq? @unset "v"))', 24, "Unknown capture"),
             ("; comment only", 0, "Expected a query pattern"),
             (escaped + "(#wrong? @x))", len(escaped) + 1, "Unsupported predicate or directive: wrong?")]
    for query, offset, message in cases:
        result = call("query_validate", query=query)
        assert result == dict(valid=False, diagnostic=dict(byteOffset=offset, message=message)), result


def check_group(call, root, path, language, source, cases, syntax_errors=False):
    write(root, path, source)
    scope = [Path(path).parent.as_posix()]
    queries = [dict(id=name, language=language, query=query) for name, query, _ in cases]
    batch = call("batch_search", paths=scope, queries=queries)
    assert not batch["incomplete"] and not batch["truncated"] and not batch["deadlineExceeded"], batch
    assert [item["id"] for item in batch["items"]] == [q["id"] for q in queries]
    for item, (name, query, expected) in zip(batch["items"], cases):
        for mode, args in [("source", dict(source=source)), ("path", dict(path=path)), ("paths", dict(paths=scope)),
                           ("batch", None)]:
            try:
                result = item if mode == "batch" else call(language=language, query=query, **args)
                check_matches(result, expected, source, None if mode == "source" else path, syntax_errors)
            except (AssertionError, KeyError) as error:
                raise AssertionError(f"{path}, {name}, {mode}: {error}") from error


def check_languages(call, root):
    fixtures = [
        ("c", "sample.c", "int f(void) { return _Generic(1, int: 2); }", "(generic_expression) @node",
         "_Generic(1, int: 2)"),
        ("rust", "sample.rs", "fn run() {}", "(function_item name: (identifier) @node)", "run"),
        ("javascript", "sample.jsx", "export const ui = <Box/>;", "(jsx_self_closing_element) @node", "<Box/>"),
        ("typescript", "sample.ts", "interface Box {}", "(interface_declaration name: (type_identifier) @node)", "Box"),
        ("tsx", "sample.tsx", "const ui: JSX.Element = <Box/>;", "(jsx_self_closing_element) @node", "<Box/>"),
        ("go", "sample.go", "package main\nfunc Run() {}\n", "(function_declaration name: (identifier) @node)", "Run"),
        ("java", "Sample.java", "class Box {}", "(class_declaration name: (identifier) @node)", "Box"),
    ]
    for language, file, source, query, expected in fixtures:
        assert call("query_validate", language=language, query=query)["valid"]
        check_group(call, root, "languages/" + file, language, source,
                    [("capture", query, [hit(("node", expected))])])

    header = "headers/shared.h"
    write(root, header, "typedef int Value;")
    queries = [dict(id=lang, language=lang, query="(type_definition declarator: (type_identifier) @name)")
               for lang in ("c", "cpp")]
    result = call("batch_search", queries=queries, paths=["headers", header])
    assert result["stats"] == dict(filesVisited=1, filesRead=1, filesParsed=1, treeBuilds=2, queryExecutions=2), result
    for item, query in zip(result["items"], queries):
        assert item["id"] == query["id"] and item["matches"] == call(
            language=query["language"], query=query["query"], paths=["headers"])["matches"]
        assert item["matches"][0]["captures"][0]["text"] == "Value", item
    write(root, header, "template<class T> struct Value {};")
    assert call("syntax_tree", path=header, language="c")["hasSyntaxErrors"]
    assert not call("syntax_tree", path=header, language="cpp")["hasSyntaxErrors"]


def check_skill_examples(executable):
    repo = Path(__file__).resolve().parents[1]
    skill = repo / "integrations/opencode-v2/.opencode/skills/structural-search/SKILL.md"
    examples = re.findall(r"^```json\n(.*?)\n```$", skill.read_text(encoding="utf-8"), re.M | re.S)
    qt, python_batch = map(json.loads, examples)
    with session(executable, repo / "tests/fixtures/opencode") as call:
        match, = call(**qt)["matches"]
        assert [c["text"] for c in match["captures"]] == ["QObject", "connect"], match
        assert match["path"] == qt["path"]
        batch = call("batch_search", **python_batch)
        assert not batch["incomplete"] and not batch["truncated"] and batch["stats"]["treeBuilds"] == 2, batch
        actual = {item["id"]: sorted((m["path"], m["captures"][0]["text"]) for m in item["matches"])
                  for item in batch["items"]}
        assert actual == {"functions": [("src/task.py", "dispatch"), ("src/worker.py", "refresh")],
                          "classes": [("src/worker.py", "Worker")]}, actual
        python_batch["queries"][1]["query"] = "(missing_node)"
        python_batch["maxTotalMatches"] = 1
        partial = call("batch_search", **python_batch)
        functions, classes = partial["items"]
        assert partial["truncated"] and partial["incomplete"], partial
        assert functions["truncated"] and len(functions["matches"]) == 1, functions
        assert classes["incomplete"] and classes["error"]["byteOffset"] == 1 and not classes["matches"], classes


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    check_skill_examples(executable)
    with tempfile.TemporaryDirectory() as temp, session(executable, Path(temp)) as call:
        root = Path(temp)
        check_languages(call, root)
        check_tree(call, root)
        check_query_diagnostics(call)
        fields = "!alias !alternative !argument !arguments !attribute !cause !condition !consequence"
        check_group(call, root, "parents/demo.py", "python", "def hello():\n    x = y\n    return f(z)\n", [
            ("expression-parent", "(expression (identifier) @id) @parent",
             [hit(("parent", "f(z)"), ("id", "f"))]),
            ("parent-without-left", "(_ !left (identifier) @id) @parent",
             [hit(("parent", "def hello():\n    x = y\n    return f(z)"), ("id", "hello")),
              hit(("parent", "f(z)"), ("id", "f")), hit(("parent", "(z)"), ("id", "z"))]),
            ("eight-fields", f"(function_definition {fields} name: (identifier) @id)", [hit(("id", "hello"))]),
            ("ninth-field", f"(function_definition {fields} !name) @fn", []),
        ])
        for keyword in ("M", "MISS", "MISSIN"):
            validation = call("query_validate", query=f"({keyword})")
            assert not validation["valid"] and validation["diagnostic"]["byteOffset"] == 1, validation
        assert call("query_validate", query="(MISSING)")["valid"]
        check_group(call, root, "missing/declaration.cpp", "cpp", "int x",
                    [("missing-semicolon", "(MISSING) @token", [hit(("token", ""))])], syntax_errors=True)
        check_group(call, root, "python/service.py", "python", PYTHON_SOURCE, PYTHON_QUERIES)
        check_group(call, root, "cpp/service.cpp", "cpp", CPP_SOURCE, CPP_QUERIES)
        check_group(call, root, "pairs/compare.py", "python", PAIR_SOURCE, PAIR_QUERIES)
        check_group(call, root, "recovery/partial.py", "python", "def café():\r\n    pass\r\nvalue = )\r\n",
                    [("recovered-tree", "(ERROR) @error\n(function_definition) @fn",
                      [hit(("error", "value = )")), hit(("fn", "def café():\r\n    pass"), pattern=1)])],
                    syntax_errors=True)
