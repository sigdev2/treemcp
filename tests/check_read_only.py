"""Run all five MCP tools with a read-only workspace and working directory."""

import argparse
from contextlib import contextmanager
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from check_workspace import captures, wire_session


def drop_write_override():
    # Root in a build container can bypass mode bits. Drop just CAP_DAC_OVERRIDE
    # for the fixture and its descendants before creating any files.
    if sys.platform == "linux" and os.geteuid() == 0:
        status = dict(line.split(":", 1) for line in Path("/proc/self/status").read_text().splitlines())
        if int(status["CapEff"], 16) & 2:
            os.execlp("setpriv", "setpriv", "--bounding-set=-dac_override", "--",
                      sys.executable, "-B", *sys.argv)


@contextmanager
def read_only(cwd, root, source):
    if os.name == "nt":
        # A Windows directory's read-only attribute does not prevent file creation.
        def acl(*args):
            subprocess.run(["icacls", str(cwd), *args, "/T", "/Q"], check=True,
                           stdout=subprocess.DEVNULL)
        try:
            acl("/deny", "*S-1-1-0:(WD,AD,WEA,WA,DC,DE)")
            yield
        finally:
            acl("/remove:d", "*S-1-1-0")
    else:
        try:
            source.chmod(0o444)
            root.chmod(0o555)
            cwd.chmod(0o555)
            yield
        finally:
            cwd.chmod(0o700)
            root.chmod(0o700)
            source.chmod(0o600)


def exercise(program, prefix=()):
    sizes = []
    with tempfile.TemporaryDirectory() as temp:
        cwd = Path(temp)
        root = cwd / "workspace"
        root.mkdir()
        source = root / "acceptance_fixture.py"
        contents = b"expected = 1\n"
        source.write_bytes(contents)
        with read_only(cwd, root, source):
            for probe, mode in ((cwd / "probe", "xb"), (root / "probe", "xb"), (source, "ab")):
                try:
                    with probe.open(mode):
                        pass
                except PermissionError:
                    continue
                raise AssertionError(f"Fixture allows writing {probe}. Run with ordinary user permissions.")
            with wire_session(program, root, "--workers", "2", prefix=prefix, reply_sizes=sizes) as (_, _, request):
                def call(name, **arguments):
                    reply = request("tools/call", {"name": name, "arguments": arguments})
                    assert not reply.get("isError"), reply
                    return reply["structuredContent"]

                languages = call("languages")["languages"]
                assert "python" in {entry["id"] for entry in languages}
                query = dict(language="python", query="(identifier) @name")
                assert call("query_validate", **query)["valid"]
                tree = call("syntax_tree", language="python", path=source.name)
                assert tree["nodes"][0]["type"] == "module" and not tree["hasSyntaxErrors"]
                for _ in range(2):
                    single = call("structural_search", paths=["."], **query)
                    batch = call("batch_search", paths=["."], queries=[dict(query, id="names")])
                    item, = batch["items"]
                    assert not single["incomplete"] and not batch["incomplete"]
                    assert item["id"] == "names" and item["matches"] == single["matches"]
                    assert captures(single) == [(source.name, "expected")]
                    assert batch["stats"]["filesRead"] == batch["stats"]["treeBuilds"] == 1
            assert set(cwd.rglob("*")) == {root, source} and source.read_bytes() == contents
    print(f"{os.name}: creation in cwd and workspace and writing source denied; "
          "languages, query_validate, syntax_tree, structural_search and batch_search passed")
    return sizes


if __name__ == "__main__":
    drop_write_override()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    exercise(parser.parse_args().program.resolve())
