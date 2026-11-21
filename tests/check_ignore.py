"""Check the saved Git decisions offline, or pass --git to repeat the reference run."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from check_workspace import captures, session, write


QUERY = {"id": "names", "language": "python", "query": "(identifier) @name"}


def check_scope(call, selectors, expected, diagnostic=None):
    single = call(paths=selectors)
    batch = call("batch_search", paths=selectors, queries=[QUERY])
    for response in (single, batch):
        assert response["incomplete"] == bool(diagnostic), response
        assert [item["path"] for item in response["diagnostics"]] == ([diagnostic] if diagnostic else []), response
    for result in (single, batch["items"][0]):
        assert not result["truncated"], result
        actual = {path for path, _ in captures(result)}
        assert actual == set(expected), (actual ^ set(expected), selectors)
    assert batch["stats"]["filesVisited"] == len(expected), batch


def verify_git(root, fixture, paths):
    empty = write(root.parent, "empty-config", "")
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(empty))
    git = ["git", "-c", "core.ignoreCase=false", "-c", f"core.excludesFile={empty}"]

    def run(*args, data=None):
        return subprocess.run([*git, *args], input=data, cwd=root, env=env, check=True, stdout=subprocess.PIPE).stdout

    run("init", "-q", "--template=")
    run("add", "--force", "--", "track.py", "order.py")
    excluded = run("check-ignore", "--no-index", "--stdin", "-z", data=b"".join(p.encode() + b"\0" for p in paths))
    actual = set(excluded.decode().split("\0")[:-1])
    assert actual == set(fixture["excluded"]), actual ^ set(fixture["excluded"])
    print(run("--version").decode().strip(), flush=True)


def check_reference(executable, root, use_git):
    fixture = json.loads(Path(__file__).with_name("fixtures").joinpath("workspace-ignore.json").read_text("utf-8"))
    paths = fixture["excluded"] + fixture["visible"]
    for path in paths:
        write(root, path, "found\n")
    for name, lines in fixture["rules"].items():
        write(root, name, "\n".join(lines))
    if use_git:
        verify_git(root, fixture, paths)
    with session(executable, root) as call:
        check_scope(call, ["."], fixture["visible"])
        assert captures(call(paths=["."], include=["n?.py"])) == [("né.py", "found")]
        check_scope(call, ["prune/keep.py", "scope/again.py", "track.py"], ["scope/again.py"])
        for path in fixture["excluded"]:
            assert call(path=path).get("isError"), path
        for path in fixture["visible"]:
            assert captures(call(path=path)) == [(path, "found")], path
        write(root, ".ignore", "!track.py\norder.py\nscope/again.py\n")
        check_scope(call, ["track.py", "order.py", "scope/again.py"], ["track.py", "scope/again.py"])
        write(root, "scope/.ignore", "again.py\n")
        check_scope(call, ["scope/again.py"], [])


def check_limits(executable, root):
    files = ["ok.py", "nested/ok.py"]
    for path in files:
        write(root, path, "found")
    write(root, ".ignore", "unused\n" * 254)
    with session(executable, root) as call:
        write(root, "nested/.gitignore", "unused\n")
        write(root, "nested/.ignore", "é" * 256 + "\n")
        check_scope(call, ["."], files)
        for invalid in ("é" * 256 + "x\n", "unused\nunused\n", b"\xff", b"comment\x00", b"#" * 65537):
            write(root, "nested/.ignore", invalid)
            check_scope(call, ["."], ["ok.py"], "nested")
            assert call(path="nested/ok.py").get("isError")
        write(root, "nested/.ignore", b"#" * 65536)
        check_scope(call, ["."], files)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--git", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        check_reference(args.executable.resolve(), Path(temp) / "reference", args.git)
        check_limits(args.executable.resolve(), Path(temp) / "limits")
