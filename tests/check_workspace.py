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


@contextmanager
def session(executable, root):
    with subprocess.Popen(
        [executable, "--workspace-root", root], cwd=root.parent,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, encoding="utf-8",
    ) as child:
        request_id = 0

        def request(method, params, notify=False):
            nonlocal request_id
            frame = {"jsonrpc": "2.0", "method": method, "params": params}
            if not notify:
                request_id += 1
                frame["id"] = request_id
            child.stdin.write(json.dumps(frame) + "\n")
            child.stdin.flush()
            if not notify:
                reply = json.loads(child.stdout.readline())
                assert reply["id"] == request_id, reply
                return reply["result"]

        def call(name="structural_search", **args):
            args["language"] = "python"
            if name == "structural_search":
                args.setdefault("query", "(identifier) @name")
            result = request("tools/call", {"name": name, "arguments": args})
            if result.get("isError"):
                return result
            data = result["structuredContent"]
            assert data == json.loads(result["content"][0]["text"])
            return data

        try:
            request("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                    "clientInfo": {"name": "workspace-test", "version": "1"}})
            request("notifications/initialized", {}, notify=True)
            yield call
        finally:
            child.stdin.close()
            assert child.wait(timeout=5) == 0
            assert child.stdout.read() == "" and child.stderr.read() == ""


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
    for path in ("", ".", "..", "src/../a.py", "src/./a.py", "src//a.py", "src/", "/a.py",
                 str(root / "a.py"), "C:relative", "a.py:stream", r"\\.\NUL", r"\\host\share",
                 "aux.txt", "COM\u00b9.py", "a\x00b", "trailing.", "src\\a.py"):
        assert call("syntax_tree", path=path).get("isError"), repr(path)
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
        with (root / "test.py").open("ab") as writer:
            assert call(path="test.py").get("isError")
            result = call(paths=["."], include=["src/sub/local.py"])
            assert not result["incomplete"] and captures(result) == [("src/sub/local.py", "identifier")], result
            writer.write(b"")
    else:
        (root / "filelink.py").symlink_to(outside / "outside.py")
        os.mkfifo(root / "fifo.py")
        with socket.socket(socket.AF_UNIX) as local:
            local.bind(str(root / "socket.py"))
            assert call(path="socket.py").get("isError")
        assert call(path="fifo.py").get("isError")
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


def check_directory_swap(call, root, outside):
    # Rename either an ordinary directory or an outside link into the same slot.
    live = root / "race"
    parked = root / "parked"
    redirect = root / "redirect"
    write(live, "outside.py", "inside\n")
    junction_or_link(outside, redirect)
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
    finally:
        stop.set()
        thread.join(5)
    assert not thread.is_alive() and not errors, errors


def main():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "workspace"
        root.mkdir()
        outside = root.parent / "outside"
        with session(executable, root) as call:
            check_inputs(call, root)
            check_freshness(call, root)
            check_filters(call, root, outside)
            check_unicode_names(call, root)
            check_directory_swap(call, root, outside)
            if os.name != "nt":
                pinned = root.rename(root.parent / "pinned")
                write(root, "test.py", "wrong_root\n")
                assert captures(call(path="test.py")) == [("test.py", "identifier")]
                assert pinned.is_dir()


if __name__ == "__main__":
    main()
