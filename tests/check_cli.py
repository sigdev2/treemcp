"""Compare one-shot tool calls with MCP and verify the CLI process contract."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from check_workspace import captures, wire_session, write


def check(executable, root):
    example = "src/café.py"
    write(root, example, "def café():\n    return 1\n\nclass Worker:\n    pass\n")
    write(root, "src/hidden.py", "def ignored(): pass\n")
    write(root, ".gitignore", "hidden.py\n")
    write(root, "generated/other.py", "def generated(): pass\n")
    options = ("--ignore-dir", "generated", "--workers", "2")
    command = [executable, "--workspace-root", root, *options]

    def invoke(tool, *arguments, data=b"", exit_code=0, message=None):
        run = subprocess.run([*command, "call", tool, *arguments], input=data,
                             cwd=root.parent, capture_output=True, timeout=10)
        assert run.returncode == exit_code, run
        if exit_code:
            assert run.stdout == b"" and message in run.stderr, run
            return None
        assert run.stderr == b"" and run.stdout.endswith(b"\n"), run
        assert run.stdout.count(b"\n") == 1, run
        return json.loads(run.stdout.decode("utf-8"))

    function = "(function_definition name: (identifier) @name)"
    language = {"language": "python"}
    single = {**language, "path": example, "query": function}
    scoped = {**language, "paths": ["."], "query": function}
    batch = {"paths": ["."], "queries": [
        {"id": "functions", **language, "query": function},
        {"id": "classes", **language, "query": "(class_definition name: (identifier) @name)"},
    ]}
    with wire_session(executable, root, *options) as (_, _, request):
        def mcp(name, **args):
            return request("tools/call", {"name": name, "arguments": args})["structuredContent"]

        for name, args in (
            ("languages", {}),
            ("query_validate", {**language, "query": function}),
            ("syntax_tree", {**language, "path": example, "maxDepth": 2}),
            ("structural_search", single),
            ("structural_search", scoped),
            ("batch_search", batch),
        ):
            encoded = json.dumps(args, ensure_ascii=False)
            expected = mcp(name, **args)
            assert invoke(name, "--args", encoded) == expected
            assert invoke(name, "--args", "-", data=json.dumps(args, indent=2, ensure_ascii=False).encode()) == expected
        assert captures(invoke("structural_search", "--args", json.dumps(scoped))) == [(example, "café")]
        invalid = {**language, "query": "(missing_node)"}
        validation = invoke("query_validate", "--args", json.dumps(invalid))
        assert validation == mcp("query_validate", **invalid) and not validation["valid"]
        partial = {"paths": ["src"], "queries": [
            {"id": "names", **language, "query": "(identifier) @name", "maxMatches": 1},
            {"id": "invalid", **invalid},
        ]}
        result = invoke("batch_search", "--args", json.dumps(partial))
        assert result == mcp("batch_search", **partial)
        assert result["truncated"] and result["incomplete"] and result["items"][1]["error"]

    with subprocess.Popen([executable, "call", "languages"], cwd=root, stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        assert child.wait(timeout=5) == 0
        assert json.loads(child.stdout.read())["languages"] and not child.stderr.read()

    invoke("missing_tool", exit_code=2, message=b"Unknown tool")
    invoke("syntax_tree", "--args", json.dumps({**language, "path": "../outside.py"}),
           exit_code=1, message=b"Expected a relative workspace path")
    invoke("structural_search", "--args", json.dumps({**single, "query": "(missing_node)"}),
           exit_code=1, message=b"Unknown node type")
    for malformed in (b"", b"[]", b'{} {}', b'{"x":1,"x":2}', b'{"x":"\xff"}', b'{}\x1aextra'):
        invoke("languages", "--args", "-", data=malformed, exit_code=2, message=b"UTF-8 JSON object")
    invoke("languages", "--args", "-", data=b" " * (1024 * 1024 + 1),
           exit_code=2, message=b"1 MiB")
    invoke("languages", "--args", "-", data=b"{}" + b" " * (1024 * 1024 - 2))
    invoke("languages", "--args", "-", data=b"\xef\xbb\xbf{}\r\n")

    if os.name == "posix":
        malformed = b'{"language":"python","source":"# \xff","query":"(comment) @comment"}'
        invoke("structural_search", "--args", "-", data=malformed, exit_code=2, message=b"UTF-8 JSON object")
        for flags in (("--args", malformed), (b"--args=" + malformed,)):
            invoke("structural_search", *flags, exit_code=2, message=b"UTF-8 command-line arguments")

        reader, writer = os.pipe()
        os.close(reader)
        with os.fdopen(writer, "wb") as stdout:
            run = subprocess.run([*command, "call", "languages"], cwd=root.parent,
                                 stdout=stdout, stderr=subprocess.PIPE, timeout=10)
        assert run.returncode == 1 and b"Cannot write tool result to stdout" in run.stderr, run


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "workspace with spaces"
        check(Path(sys.argv[1]).resolve(), root)
