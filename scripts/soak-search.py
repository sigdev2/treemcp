"""Run the mixed MCP workload against a Linux Release binary and stream JSONL evidence."""

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import random
import signal
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from check_batch import complete, params, query
from check_concurrency import cancel, search
from check_workspace import wire_session, write


def memory(pid):
    fields = {}
    for line in Path(f"/proc/{pid}/status").read_text().splitlines():
        key, _, value = line.partition(":")
        if key in ("VmRSS", "VmHWM"):
            fields[key] = int(value.split()[0]) * 1024
    return fields["VmRSS"], fields["VmHWM"]


def round_trip(connection, root, rng, counts):
    send, receive, _ = connection
    paths = ["load/a.py", "load/b.py"]
    rng.shuffle(paths)
    batch = params([query("functions", "(function_definition name: (identifier) @n)")],
                   paths=paths, deadlineMs=30000)
    first = send("tools/call", batch)
    second = send("tools/call", batch)
    stopped = send("tools/call", search(path=paths[0], maxMatches=1))
    cancel(send, stopped)
    marker = f"edited_{rng.getrandbits(64):016x}"
    write(root, "marker.py", marker + "\n")
    fresh = send("tools/call", search(paths=["marker.py"]))
    fence = send("ping", {})
    pending = {first, second, fresh, fence}
    cancelled = True
    while pending:
        reply, _ = receive()
        ident = reply["id"]
        if ident == stopped and cancelled:
            assert reply["result"]["structuredContent"]["matches"], reply
            cancelled = False
            continue
        pending.remove(ident)
        if ident == second and "error" in reply:
            assert reply["error"] == {"code": -32001, "message": "Search scheduler at capacity"}, reply
            counts["overload"] += 1
        elif ident in (first, second):
            item, = complete(reply["result"]["structuredContent"])
            assert sorted((m["path"], m["captures"][0]["text"]) for m in item["matches"]) == [
                ("load/a.py", "alpha"), ("load/b.py", "beta")]
            counts["batch"] += 1
        elif ident == fresh:
            content = reply["result"]["structuredContent"]
            match, = content["matches"]
            assert not content["incomplete"] and match["captures"][0]["text"] == marker, reply
            counts["editSearch"] += 1
        else:
            assert reply["result"] == {}, reply
    counts["cancelled" if cancelled else "completedBeforeCancel"] += 1
    return fence


def run(args, root, record):
    program = args.program.resolve()
    record("start", binary=str(program), sha256=hashlib.sha256(program.read_bytes()).hexdigest(),
           revision=args.revision, seed=args.seed, durationSeconds=args.seconds,
           warmupSeconds=args.warmup, growthLimitMiB=args.growth_mib, workers=4,
           utc=datetime.now(timezone.utc).isoformat())
    for path, name in [("a", "alpha"), ("b", "beta")]:
        write(root, f"load/{path}.py", f"def {name}():\n    pass\n" + "a=b\n" * 6000)
    rng = random.Random(args.seed)
    counts = Counter()
    started = time.monotonic()
    last_sample = started
    baseline = None
    peak = rounds = sessions = 0
    latencies = []
    child = None

    def connected(process):
        nonlocal child
        child = process

    def stalled(*_):
        if child is not None and child.poll() is None:
            child.kill()
        raise TimeoutError("No completed workload round within 45 seconds")

    signal.signal(signal.SIGALRM, stalled)
    try:
        while time.monotonic() - started < args.seconds:
            sessions += 1
            signal.alarm(45)
            with wire_session(program, root, "--workers", "4", process=connected) as connection:
                while True:
                    tick = time.monotonic()
                    last_id = round_trip(connection, root, rng, counts)
                    now = time.monotonic()
                    elapsed = now - started
                    latencies.append(1000 * (now - tick))
                    rounds += 1
                    signal.alarm(45)
                    rss, high_water = memory(child.pid)
                    peak = max(peak, high_water)
                    if baseline is None and elapsed >= args.warmup:
                        baseline = peak
                    rotate = last_id + 5 > 16384
                    if now - last_sample >= args.interval or elapsed >= args.seconds or rotate:
                        latencies.sort()
                        record("sample", seconds=elapsed, rounds=rounds, session=sessions, pid=child.pid,
                               counts=dict(counts), rssBytes=rss, peakBytes=peak, baselineBytes=baseline,
                               roundP95Ms=latencies[math.ceil(0.95 * len(latencies)) - 1],
                               roundMaxMs=latencies[-1])
                        latencies.clear()
                        last_sample = now
                    if baseline is not None and peak > baseline + args.growth_mib * 1024**2:
                        raise RuntimeError(f"Peak RSS {peak} exceeds warm-up baseline {baseline} + {args.growth_mib} MiB")
                    if elapsed >= args.seconds or rotate:
                        break
            record("shutdown", session=sessions, pid=child.pid, exitCode=child.returncode)
        assert all(counts[key] for key in ("overload", "cancelled", "editSearch", "batch")), counts
        assert baseline is not None and rounds >= 2
        record("end", passed=True, seconds=time.monotonic() - started, rounds=rounds,
               counts=dict(counts), peakBytes=peak, baselineBytes=baseline, cleanShutdowns=sessions)
    except BaseException as error:
        record("end", passed=False, seconds=time.monotonic() - started, rounds=rounds,
               counts=dict(counts), peakBytes=peak, error=repr(error),
               exitCode=child.poll() if child else None)
        raise
    finally:
        signal.alarm(0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("program", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--seconds", type=int, default=86400)
    parser.add_argument("--warmup", type=int, default=60)
    parser.add_argument("--interval", type=int, default=10)
    parser.add_argument("--seed", type=int, default=7303)
    parser.add_argument("--growth-mib", type=int, default=64)
    args = parser.parse_args()
    if sys.platform != "linux" or not 0 < args.warmup < args.seconds or min(args.interval, args.growth_mib) <= 0:
        parser.error("requires Linux, 0 < warmup < seconds and positive interval and growth limit")
    with args.output.open("x", encoding="utf-8", buffering=1) as report, tempfile.TemporaryDirectory() as temp:
        def record(event, **fields):
            print(json.dumps(dict(event=event, **fields)), file=report)
        run(args, Path(temp), record)


if __name__ == "__main__":
    main()
