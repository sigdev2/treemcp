"""Exercise the workspace boundary and freshness through one real stdio session."""

from contextlib import contextmanager
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading


if not __debug__:
    raise RuntimeError("These tests require Python assertions. Unset PYTHONOPTIMIZE and omit -O.")


@contextmanager
def wire_session(executable, root, *options, default_root=False, prefix=(), reply_sizes=None):
    command = [executable, *options] if default_root else [executable, "--workspace-root", root, *options]
    with subprocess.Popen(
        [*prefix, *command], cwd=root if default_root else root.parent,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8",
    ) as child:
        request_id = 0

        def send(method, params, notify=False):
            nonlocal request_id
            frame = {"jsonrpc": "2.0", "method": method, "params": params}
            if not notify:
                request_id += 1
                frame["id"] = request_id
            child.stdin.write(json.dumps(frame) + "\n")
            child.stdin.flush()
            return request_id

        def receive():
            line = child.stdout.readline()
            size = len(line.encode("utf-8"))
            if reply_sizes is not None:
                reply_sizes.append(size)
            return json.loads(line), size

        def request(method, params):
            expected = send(method, params)
            reply, _ = receive()
            assert reply["id"] == expected, reply
            return reply["result"]

        try:
            request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                    "clientInfo": {"name": "workspace-test", "version": "1"}})
            send("notifications/initialized", {}, notify=True)
            yield send, receive, request
        finally:
            child.stdin.close()
            assert child.wait(timeout=5) == 0
            assert child.stdout.read() == "" and child.stderr.read() == ""


@contextmanager
def session(executable, root, *options):
    with wire_session(executable, root, *options) as (_, _, request):
        def call(name="structural_search", **args):
            if name != "batch_search":
                args.setdefault("language", "python")
            if name == "structural_search":
                args.setdefault("query", "(identifier) @name")
            result = request("tools/call", {"name": name, "arguments": args})
            if result.get("isError"):
                return result
            data = result["structuredContent"]
            assert data == json.loads(result["content"][0]["text"])
            return data

        yield call


def write(root, path, data):
    target = root / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data.encode("utf-8") if isinstance(data, str) else data)
    return target


def captures(result):
    return sorted((match["path"], match["captures"][0]["text"]) for match in result["matches"])


def check_unicode_names(call, root):
    write(root, "unicode/n.py", "plain\n")
    write(root, "unicode/\ufeffn.py", "prefixed\n")
    found = call(paths=["unicode"])
    assert not found["incomplete"], found
    for path, name in (("unicode/n.py", "plain"), ("unicode/\ufeffn.py", "prefixed")):
        assert captures(call(path=path)) == [(path, name)]
    assert captures(found) == [("unicode/n.py", "plain"), ("unicode/\ufeffn.py", "prefixed")], found


def junction_or_link(target, link):
    if os.name == "nt":
        import _winapi
        _winapi.CreateJunction(str(target), str(link))
    else:
        link.symlink_to(target, target_is_directory=True)


def check_inputs(call, root):
    for prefix, expected in [(78, "a" * 78 + "\U0001f600"), (79, "a" * 79)]:
        result = call(source="x", **{"a" * prefix + "\U0001f600" + "x": True})
        assert result["isError"] and result["content"][0]["text"] == "Unknown argument: " + expected, result
    for path in ("", ".", "..", "src/../a.py", "src/./a.py", "src//a.py", "src/", "/a.py",
                 str(root / "a.py"), "C:relative", "a.py:stream", r"\\.\NUL", r"\\host\share",
                 "aux.txt", "COM\u00b9.py", "a\x00b", "trailing.", "src\\a.py"):
        result = call("syntax_tree", path=path)
        message = "Expected a relative workspace path" if path else "path is outside its UTF-8 byte limit"
        assert result.get("isError") and result["content"][0]["text"] == message, (path, result)
        if path != ".":
            assert call(paths=["a.py", path]).get("isError"), repr(path)
    for bad in ({}, {"source": "", "path": "a.py"}, {"paths": []}, {"paths": ["a.py", 4]},
                {"paths": ["."], "include": ["../*"]}, {"path": "a.py", "exclude": []}):
        assert call(**bad).get("isError"), bad


def check_freshness(call, root):
    query = {"paths": [".", "src", "src/a.py", "src"], "include": ["src/**/*.py"]}
    path = write(root, "src/a.py", "# \u03c0\r\nold = 1\n")
    first = call(**query)
    assert captures(first) == [("src/a.py", "old")], first
    assert first["matches"][0]["captures"][0]["range"]["startByte"] == 6
    assert not first["incomplete"] and not first["truncated"]
    write(root, "src/a.py", "new = 1\n")
    assert captures(call(path="src/a.py")) == [("src/a.py", "new")]
    write(root, "replace.py", "replacement = 1\n").replace(path)
    assert captures(call(**query)) == [("src/a.py", "replacement")]
    tree = call("syntax_tree", path="src/a.py", maxDepth=0)
    assert tree["nodes"][0]["range"]["endByte"] == path.stat().st_size
    path.unlink()
    deleted = call(**query)
    assert captures(deleted) == [] and not deleted["incomplete"], deleted
    assert call(path="src/a.py").get("isError")
    missing = call(paths=["src/a.py"])
    assert missing["incomplete"] and missing["diagnostics"]


def check_scope_boundaries(executable, root):
    suffixes = {"cpp": ["cpp", "cc", "cxx", "hpp", "hh", "hxx", "h"], "python": ["py", "pyi"]}
    for suffix in [*suffixes["cpp"], *suffixes["python"], "txt"]:
        write(root, f"by_type/sample.{suffix}", "value")
    write(root, "by_type/uppercase.PY", "uppercase")
    # A component at depth 64 is eligible. One more component exceeds the traversal bound.
    parent = "d/" * 63
    edge = parent + "edge.py"
    deep = parent + "d/deep.py"
    for path in [edge, deep, "top.py", "sub/top.py", "sub/nested/top.py"]:
        write(root, path, "value")
    with wire_session(executable, root, default_root=True) as (_, _, request):
        reply = request("tools/call", dict(name="structural_search", arguments=dict(
            path="top.py", language="python", query="(identifier) @name")))
        assert captures(reply["structuredContent"]) == [("top.py", "value")], reply
    with session(executable, root) as call:
        for lang, extensions in suffixes.items():
            expected = sorted((f"by_type/sample.{suffix}", "value") for suffix in extensions)
            assert captures(call(language=lang, paths=["by_type"])) == expected
        assert captures(call(path="by_type/sample.txt")) == [("by_type/sample.txt", "value")]
        for glob, paths in [("/top.py", ["top.py"]), ("sub/**", ["sub/nested/top.py", "sub/top.py"])]:
            assert captures(call(paths=["top.py", "sub"], include=[glob])) == [(p, "value") for p in paths]
        for glob in ["[", "a\\b", "a\x00b"]:
            error = call(paths=["sub"], include=[glob])
            assert error["isError"] and error["content"][0]["text"] == "Invalid workspace glob", error
        assert captures(call(path=edge)) == [(edge, "value")]
        invalid = call(paths=[deep])
        assert invalid["isError"] and invalid["content"][0]["text"] == "Expected a relative workspace path", invalid
        partial = call(paths=["d"])
        assert partial["incomplete"] and captures(partial) == [(edge, "value")], partial
        assert partial["diagnostics"] == [dict(path=deep, message="Workspace path limit exceeded")], partial

        # 255 UTF-16 units precede the supplementary character in each missing path.
        folder = "a" * 64
        (root / folder).mkdir()
        prefix = folder + "/" + "b" * 190
        missing = prefix + "\U0001f600.py"
        paths = [missing] + [f"missing{i:02}.py" for i in range(20)]
        q = dict(id="names", language="python", query="(identifier) @n")
        for result in [call(paths=paths), call("batch_search", paths=paths, queries=[q])]:
            assert result["incomplete"] and not result["truncated"], result
            diagnostics = result["diagnostics"]
            assert len(diagnostics) == 16 and all(d["message"] == "Workspace path not found" for d in diagnostics)
            reported = [d["path"] for d in diagnostics]
            assert len(set(reported)) == 16 and set(reported) <= {prefix, *paths[1:]}, reported
        for result in [call(paths=[missing]), call("batch_search", paths=[missing], queries=[q])]:
            assert result["diagnostics"] == [dict(path=prefix, message="Workspace path not found")], result


def check_filters(call, root, outside):
    write(root, ".gitignore", "# root\n/blocked/\nskip*.py\n!skip_keep.py\n")
    write(root, "src/.gitignore", "/local.py\nreinclude/**\n!reinclude/ok.py\n")
    for name in ("blocked/inside.py", "src/local.py", "src/reinclude/bad.py", "src/reinclude/ok.py",
                 "src/sub/local.py", "src/skip_this.py", "src/skip_keep.py", ".hidden.py", ".git/hidden.py",
                 "src/vendor/filtered.py", "wrong.cpp", "test.py"):
        write(root, name, "identifier\n")
    for name, data in (("nul.py", b"a\x00b"), ("invalid.py", b"\xff"), ("binary.py", b"\x01")):
        write(root, name, data)
    write(outside, "outside.py", "secret\n")
    junction_or_link(outside, root / "link")
    if os.name == "nt":
        import ctypes
        hidden = write(root, "win_hidden.py", "secret\n")
        assert ctypes.windll.kernel32.SetFileAttributesW(str(hidden), 2)
        with (root / "test.py").open("ab"):
            assert call(path="test.py").get("isError")
            result = call(paths=["."], include=["src/sub/local.py"])
            assert not result["incomplete"] and captures(result) == [("src/sub/local.py", "identifier")], result
        print("Windows: hidden attribute and write-sharing conflict exercised")
    else:
        (root / "filelink.py").symlink_to(outside / "outside.py")
        os.mkfifo(root / "fifo.py")
        with socket.socket(socket.AF_UNIX) as local:
            local.bind(str(root / "socket.py"))
            assert call(path="socket.py").get("isError")
        assert call(path="fifo.py").get("isError")
        print("POSIX: FIFO and Unix socket rejected")
    assert call(path="link/outside.py").get("isError")
    for path in ("src/reinclude/bad.py", "blocked/inside.py", ".hidden.py", "src/local.py", "nul.py"):
        assert call(path=path).get("isError"), path
    expected = ["src/reinclude/ok.py", "src/skip_keep.py", "src/sub/local.py", "test.py"]
    scope = {"paths": [".", "src", "src/skip_keep.py"], "include": ["**/*.p[y]"], "exclude": ["**/vendor/"]}
    result = call(**scope)
    assert [p for p, _ in captures(result)] == expected and not result["incomplete"], result
    limited = call(**scope, maxMatches=1)
    assert len(limited["matches"]) == 1 and limited["truncated"]
    write(root, "src/.gitignore", "*.py\n")
    assert [p for p, _ in captures(call(**scope))] == ["test.py"]
    ignore = write(root, ".ignore", "/test.py\n")
    filtered = call(**scope)
    assert not filtered["incomplete"] and captures(filtered) == [], filtered
    ignore.unlink()
    write(root, "src/.gitignore", b"\xff")
    partial = call(**scope)
    assert partial["incomplete"] and partial["diagnostics"][0]["path"] == "src"
    assert [p for p, _ in captures(partial)] == ["test.py"]
    too_big = write(root, "large.py", b"")
    with too_big.open("wb") as output:
        output.truncate(8 * 1024 * 1024 + 1)
    assert call(path="large.py").get("isError")
    partial = call(paths=["large.py", "test.py"])
    assert partial["incomplete"] and partial["diagnostics"] and len(partial["matches"]) == 1


def check_search_exclusions(executable, root):
    defaults = ["docs", "src/cmake", "build", "src/deep/CMakeFiles", "dist", "src/coverage", "src/__pycache__"]
    custom = ["generated", "src/deep/cache.py"]
    always_visible = ["build_tools/example.py", "src/Docs/example.py", "src/generated_tools/example.py",
                      "cache.py", "src/keep.py", "nested/skip.py"]
    for directory in defaults + custom:
        write(root, directory + "/example.py", "found")
    for path in always_visible + [".hidden/example.py", "src/.hidden.py", "src/ignored.py", "src/request.py",
                                  "src/skip.py", "src/artifacts/example.py"]:
        write(root, path, "found")
    write(root, ".ignore", "artifacts/\n")
    write(root, "src/artifacts/.gitignore", b"\xff")
    write(root, "src/.gitignore", "/ignored.py\n/keep.py\n")
    local_ignore = write(root, "src/.ignore", "/skip.py\n!keep.py\n")
    if os.name == "nt":
        import ctypes
        assert ctypes.windll.kernel32.SetFileAttributesW(str(local_ignore), 2)

    query = {"id": "names", "language": "python", "query": "(identifier) @name"}
    for options, skipped, visible in [
        ([], defaults, custom),
        (["--ignore-dir", "generated", "--ignore-dir", "cache.py"], custom, defaults),
    ]:
        # Entering this subtree would read the invalid ignore file and mark the search incomplete.
        invalid = write(root, skipped[-1] + "/.gitignore", b"\xff")
        with session(executable, root, *options) as call:
            expected = sorted(always_visible + [directory + "/example.py" for directory in visible])
            for paths, wanted in [(["."], expected), ([skipped[0], skipped[-1] + "/example.py"], []),
                                  (["src/skip.py", "src/artifacts/example.py"], [])]:
                scope = {"paths": paths, "exclude": ["request.py"]}
                single = call(**scope)
                batch = call("batch_search", queries=[query], **scope)
                assert not single["incomplete"] and not batch["incomplete"], (single, batch)
                for result in [single, batch["items"][0]]:
                    assert [path for path, _ in captures(result)] == wanted, result
                assert batch["stats"]["filesVisited"] == len(wanted), batch
            path = skipped[0] + "/example.py"
            assert captures(call(path=path)) == [(path, "found")]
            assert call(path="src/skip.py").get("isError")
            assert call(path="src/artifacts/example.py").get("isError")
        invalid.unlink()


def check_explicit_access(call, root):
    if os.name == "nt":
        wrong_case = write(root, "case/.Ignore", "*.py\n")
        write(root, "case/.GITIGNORE", "*.py\n")
        write(root, "case/test.py", "visible")
        result = call(paths=["case"])
        assert not result["incomplete"] and not result["diagnostics"], result
        assert captures(result) == [("case/test.py", "visible")]
        assert captures(call(path="case/test.py")) == captures(result)
        with wrong_case.open("ab"):
            busy = call(paths=["case"])
            assert not busy["incomplete"] and captures(busy) == captures(result), busy
        exact = write(root, "other/.ignore", "*.py\n")
        with exact.open("ab"):
            busy = call(paths=["other"])
            assert busy["incomplete"] and busy["diagnostics"], busy
    else:
        path = "traverse/only.py"
        folder = write(root, path, "visible").parent
        write(root, "traverse/.ignore", "hidden.py\n")
        write(root, "traverse/hidden.py", "hidden")
        folder.chmod(0o111)
        try:
            assert captures(call(path=path)) == [(path, "visible")]
            assert call("syntax_tree", path=path)["nodes"]
            assert call(path="traverse/hidden.py").get("isError")
            if os.geteuid() != 0:
                result = call(paths=["traverse"])
                assert result["incomplete"] and result["matches"] == [], result
                assert result["diagnostics"] == [dict(path="traverse", message="Cannot enumerate workspace directory")]
                print("POSIX: explicit traversal and denied directory listing exercised")
        finally:
            folder.chmod(0o755)


def check_directory_swap(call, root, outside):
    # Rename either an ordinary directory or an outside link into the same slot.
    live = root / "race"
    parked = root / "parked"
    redirect = root / "redirect"
    write(live, "outside.py", "inside\n")
    junction_or_link(outside, redirect)
    assert captures(call(path="race/outside.py")) == [("race/outside.py", "inside")]
    assert call(path="redirect/outside.py").get("isError")
    stop = threading.Event()
    swapped = threading.Event()
    errors = []

    def swap():
        try:
            while not stop.is_set():
                for source, dest in ((live, parked), (redirect, live), (live, redirect), (parked, live)):
                    while True:
                        try:
                            source.rename(dest)
                            break
                        except PermissionError:
                            if stop.wait(0.001):
                                return
                    if source == redirect:
                        swapped.set()
        except Exception as error:
            errors.append(error)
            swapped.set()

    thread = threading.Thread(target=swap)
    thread.start()
    try:
        assert swapped.wait(5)
        for _ in range(60):
            result = call(path="race/outside.py")
            assert result.get("isError") or captures(result) == [("race/outside.py", "inside")], result
            walked = call(paths=["race"])
            assert captures(walked) in ([], [("race/outside.py", "inside")]), walked
    finally:
        stop.set()
        thread.join(5)
    assert not thread.is_alive() and not errors, errors
    print("Directory replacement during explicit and recursive selection:",
          "junction" if os.name == "nt" else "symlink")


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as temp:
        check_search_exclusions(executable, Path(temp) / "exclusions")
        check_scope_boundaries(executable, Path(temp) / "scope")
        root = Path(temp) / "workspace"
        root.mkdir()
        outside = root.parent / "outside"
        with session(executable, root) as call:
            check_inputs(call, root)
            check_freshness(call, root)
            check_filters(call, root, outside)
            check_explicit_access(call, root)
            check_unicode_names(call, root)
            check_directory_swap(call, root, outside)
            if os.name != "nt":
                root.rename(root.parent / "pinned")
                write(root, "test.py", "wrong_root\n")
                assert captures(call(path="test.py")) == [("test.py", "identifier")]
                print("POSIX: renamed root remained pinned")


if __name__ == "__main__":
    main()
