"""Check Tree-sitter query results against explicit expectations through MCP stdio."""

from collections import Counter
from pathlib import Path
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


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as temp, session(executable, Path(temp)) as call:
        root = Path(temp)
        check_group(call, root, "python/service.py", "python", PYTHON_SOURCE, PYTHON_QUERIES)
        check_group(call, root, "cpp/service.cpp", "cpp", CPP_SOURCE, CPP_QUERIES)
        check_group(call, root, "pairs/compare.py", "python", PAIR_SOURCE, PAIR_QUERIES)
        check_group(call, root, "recovery/partial.py", "python", "def café():\r\n    pass\r\nvalue = )\r\n",
                    [("recovered-tree", "(ERROR) @error\n(function_definition) @fn",
                      [hit(("error", "value = )")), hit(("fn", "def café():\r\n    pass"), pattern=1)])],
                    syntax_errors=True)
