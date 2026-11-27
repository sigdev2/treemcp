"""Check the required CTest suites in a preset's --show-only=json-v1 output."""

import argparse
import json
from pathlib import Path


COMMON = {
    "bootstrap.runtime", "bootstrap.cli", "bootstrap.broken-grammar",
    "protocol.decoder", "protocol.stdio", "search.stdio", "search.cli",
    "search.queries", "scheduler.workers", "scheduler.stdio",
    "workspace.ignore-pattern", "workspace.ignore", "workspace.stdio",
    "batch.correctness", "batch.corpus", "scheduler.publication",
    "workspace.changed-read", "workspace.read-only", "workspace.entry-replacement",
}
LINUX = {"batch.shared-work", "workspace.traversal-limits"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("preset", choices=("windows-debug", "windows-release",
                                           "linux-debug", "linux-release", "linux-asan"))
    parser.add_argument("inventory", type=Path)
    args = parser.parse_args()
    required = COMMON.copy()
    if args.preset.startswith("linux-"):
        required.update(LINUX)
    if args.preset == "linux-asan":
        required.update(("protocol.session-memory", "search.request-cleanup"))

    tests = json.loads(args.inventory.read_text(encoding="utf-8"))["tests"]
    enabled = {test["name"] for test in tests
               if not any(prop["name"] == "DISABLED" and prop["value"]
                          for prop in test.get("properties", []))}
    missing = required - enabled
    if missing:
        raise SystemExit(f"{args.preset} missing or disabled suites: {', '.join(sorted(missing))}")
    print(f"{args.preset}: all {len(required)} required suites enabled")


if __name__ == "__main__":
    main()
