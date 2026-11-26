"""Report GNU gcov coverage of project sources in a CMake build after running CTest."""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--gcov", default="gcov", help="gcov executable matching the build compiler")
    parser.add_argument("--missed", action="store_true", help="show uncovered line numbers per file")
    args = parser.parse_args()
    src = Path(__file__).resolve().parents[1] / "src"
    objects = args.build.resolve() / "CMakeFiles"
    lines = defaultdict(dict)
    branches = defaultdict(list)
    for target in ("treemcp_core", "treemcp"):
        notes = sorted((objects / (target + ".dir") / "src").rglob("*.gcno"))
        if not notes:
            parser.error(f"No gcov notes for {target}. Configure a fresh GCC build with --coverage.")
        for note in notes:
            if not note.with_suffix(".gcda").is_file():
                parser.error(f"No profile for {note.name}. Run CTest in the instrumented build.")
            report = json.loads(subprocess.check_output([args.gcov, "-jbt", str(note)]))
            for file in report["files"]:
                path = Path(file["file"])
                if not path.is_absolute():
                    path = Path(report["current_working_directory"]) / path
                if not path.is_relative_to(src):
                    continue
                name = path.relative_to(src).as_posix()
                for line in file["lines"]:
                    number = line["line_number"]
                    lines[name][number] = lines[name].get(number, False) or line["count"] > 0
                    branches[name].extend(b["count"] > 0 for b in line["branches"] if not b["throw"])
    if not any(lines.values()):
        parser.error(f"No measured source lines under {src}. Use the instrumented checkout.")

    def ratio(hits, total):
        return f"{hits}/{total} ({100 * hits / total:.2f}%)" if total else "0/0"

    print("Source\tExecutable lines\tBranch outcomes")
    for name, counts in sorted(lines.items()):
        print(name, ratio(sum(counts.values()), len(counts)), ratio(sum(branches[name]), len(branches[name])), sep="\t")
        if args.missed:
            print("  Uncovered lines:", *sorted(n for n, hit in counts.items() if not hit))
    print("TOTAL", ratio(sum(sum(c.values()) for c in lines.values()), sum(map(len, lines.values()))),
          ratio(sum(map(sum, branches.values())), sum(map(len, branches.values()))), sep="\t")


if __name__ == "__main__":
    main()
