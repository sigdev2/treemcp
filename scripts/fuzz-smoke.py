"""Seed and run each instrumented treemcp harness with a fixed per-target budget."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

TESTS = Path(__file__).resolve().parents[1] / "tests"
sys.path.insert(0, str(TESTS))
from check_search_queries import CPP_QUERIES, CPP_SOURCE, PYTHON_QUERIES, PYTHON_SOURCE


def seeds(surface):
    if surface == "rpc":
        # Protocol decoder and stdio fixture cases, including a partial final frame.
        yield b'{"jsonrpc":"2.0","method":"ping","id":9223372036854775807}\r\n'
        yield b'{"jsonrpc":"2.0","id":1,"\\u0069d":2,"method":"ping"}\n'
        yield b'one\ntwo\r\n{\r}\npartial'
    elif surface == "workspace":
        fixtures = json.loads((TESTS / "fixtures/workspace-ignore.json").read_text(encoding="utf-8"))
        for path in fixtures["visible"] + fixtures["excluded"]:
            yield f"**/*.py\n{path}".encode()
        for group in fixtures["rules"].values():
            for rule in group:
                yield f"{rule}\nsrc/worker.py".encode()
        for path in ("src/../a.py", "C:relative", "a\0b"):
            yield f"*\n{path}".encode()
    else:
        for language, queries, source in [(b"P", PYTHON_QUERIES, PYTHON_SOURCE), (b"C", CPP_QUERIES, CPP_SOURCE)]:
            for _, query, _ in queries:
                yield language + query.encode() + b"\0" + source.encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seconds", type=int, default=15, help="Mutation time per target")
    parser.add_argument("--seed", type=int, default=7303)
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error("--seconds must be positive")
    env = dict(os.environ, UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    for surface, limit in [("rpc", 8192), ("workspace", 4096), ("query", 8192)]:
        output = args.output.resolve() / surface
        corpus = output / "corpus"
        corpus.mkdir(parents=True, exist_ok=True)
        for data in seeds(surface):
            (corpus / hashlib.sha256(data).hexdigest()).write_bytes(data)
        harness = args.build.resolve() / "tests/fuzz" / f"fuzz-{surface}"
        flags = [str(harness), f"-seed={args.seed}", f"-max_len={limit}", "-timeout=10",
                 "-rss_limit_mb=1536", f"-artifact_prefix={output}/"]
        with (output / "run.log").open("w") as log:
            def execute(arguments, timeout):
                subprocess.run(flags + arguments, stdout=log, stderr=subprocess.STDOUT,
                               env=env, timeout=timeout, check=True)
            for regression in sorted((TESTS / "fuzz/regressions" / surface).glob("*.bin")):
                execute([str(regression)], 30)
                data = regression.read_bytes()
                (corpus / hashlib.sha256(data).hexdigest()).write_bytes(data)
            print(f"{surface}: {args.seconds}s, log {log.name}", flush=True)
            execute([f"-max_total_time={args.seconds}", str(corpus)], args.seconds + 45)


if __name__ == "__main__":
    main()
