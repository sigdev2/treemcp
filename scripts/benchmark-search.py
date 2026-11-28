#!/usr/bin/env python3
"""Compare Release search implementations in persistent MCP sessions."""

import argparse
from contextlib import ExitStack
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from check_batch import CORPUS_CASES, corpus_files, corpus_queries, params, query
from check_workspace import wire_session, write


WORKERS = (1, 2, 4, 8)
METHOD = "tools/call"
CORPUS_VERSION = 1
CORPUS_HASHES = {
    "1000": "aed288ea1b1cf760c11a864ac8560730e1e48f18db8898e7203ddd6208cd2e00",
    "10000": "e066771f8b62f2ae5ed1741b95a3344189df763e9b70c3d5532515b547b5ac21",
    "large": "3990ea5ab66d2eb9bf166dcab2aa2e111ee645619440a320f2fde867e7c58c11",
    "dense": "48f0dfad989f94f5d0272e0a3bb3f30e932e91ada337806646f77db05606000b",
    "long": "a07f8551fe7438e9d52abb6928bd5abf30aea444c3840e63cc502283eee47ec3",
    "wide": "a27bbd293d18206ffe0e23c89a0bc08e1c9e5d7ccdfdfe88550e40a9d492b911",
}


def process_resources(pid):
    if sys.platform == "linux":
        proc = Path(f"/proc/{pid}")
        peak = next(line for line in (proc / "status").read_text().splitlines() if line.startswith("VmHWM:"))
        return int(peak.split()[1]) * 1024, len(os.listdir(proc / "fd"))
    if os.name == "nt":
        import ctypes as c
        from ctypes import wintypes as w

        class Memory(c.Structure):
            _fields_ = [("size", w.DWORD), ("faults", w.DWORD), ("peakWorkingSet", c.c_size_t),
                        ("remainingFields", c.c_size_t * 7)]

        kernel = c.WinDLL("kernel32", use_last_error=True)
        psapi = c.WinDLL("psapi", use_last_error=True)
        kernel.OpenProcess.argtypes, kernel.OpenProcess.restype = (w.DWORD, w.BOOL, w.DWORD), w.HANDLE
        kernel.CloseHandle.argtypes = (w.HANDLE,)
        kernel.GetProcessHandleCount.argtypes = (w.HANDLE, c.POINTER(w.DWORD))
        psapi.GetProcessMemoryInfo.argtypes = (w.HANDLE, c.POINTER(Memory), w.DWORD)
        handle = kernel.OpenProcess(0x0410, False, pid)
        if not handle:
            raise c.WinError(c.get_last_error())
        try:
            memory = Memory()
            handles = w.DWORD()
            if not (psapi.GetProcessMemoryInfo(handle, c.byref(memory), c.sizeof(memory)) and
                    kernel.GetProcessHandleCount(handle, c.byref(handles))):
                raise c.WinError(c.get_last_error())
            return memory.peakWorkingSet, handles.value
        finally:
            kernel.CloseHandle(handle)
    raise RuntimeError("Resource sampling supports Windows and Linux")


def measured_exchange(connection, mode, specs, pid, resources):
    handles = []
    errors = []
    done = threading.Event()

    def sample():
        try:
            while not done.wait(0.01):
                handles.append(process_resources(pid)[1])
        except OSError as error:
            errors.append(error)

    if resources:
        _, handles_before = process_resources(pid)
        poller = threading.Thread(target=sample)
        poller.start()
    try:
        payload, result = exchange(connection, mode, specs)
    finally:
        if resources:
            done.set()
            poller.join()
    if resources:
        if errors:
            raise errors[0]
        peak, handles_after = process_resources(pid)
        result.update(peakResidentBytes=peak, handlesBefore=handles_before, handlesAfter=handles_after,
                      handlesSampledMax=max([handles_before, handles_after, *handles]))
    result["pid"] = pid
    return payload, result


def single(spec, **scope):
    return {"name": "structural_search", "arguments": {
        **{key: value for key, value in spec.items() if key != "id"}, "deadlineMs": 120000, **scope}}


def generate(root):
    groups = {str(count): corpus_files(count, query_count) for count, query_count in CORPUS_CASES}
    groups.update({
        "large": ((f"large/{i}.py", b"value = 1\n" * 10000) for i in range(8)),
        "dense": ((f"dense/{i // 100}/{i}.py", b"alpha\nbeta\n" * 10) for i in range(1000)),
        "long": ((f"long/{i}.py", (b'x = "' + b"a" * 1400 + b'"\n') * 100) for i in range(8)),
    })

    def wide():
        yield "wide/.gitignore", b"drop*/\n*.txt\n"
        for i in range(128):
            yield f"wide/d{i}/.ignore", b"generated*.py\n!generated-keep.py\n"
            yield f"wide/d{i}/generated-keep.py", b"pass\n"
            for j in range(20):
                yield f"wide/d{i}/generated{j}.py", b"ignored\n"
                yield f"wide/d{i}/data{j}.txt", b"ignored\n"
                yield f"wide/d{i}/data{j}.md", b"wrong extension\n"
                yield f"wide/d{i}/drop/data{j}.py", b"ignored subtree\n"

    groups["wide"] = wide()
    hashes = {}
    for group, files in groups.items():
        digest = hashlib.sha256()
        for path, data in files:
            write(root, path, data)
            digest.update(path.encode() + b"\0" + hashlib.sha256(data).digest())
        hashes[group] = digest.hexdigest()
    return hashes


def workloads():
    def series(scope, specs):
        limits = {"paths": [scope], "deadlineMs": 120000}
        singles = [single(spec, **limits) for spec in specs]
        return singles, params(specs[:1], **limits), params(specs, **limits)

    cases = {}
    for count, query_count in CORPUS_CASES:
        specs = corpus_queries(query_count)[:2]
        singles, batch_one, batch_many = series(str(count), specs)
        cases.update({(f"{count}.{name}"): mode for name, mode in {
            "single": ("sequential", singles[:1]), "sequential": ("sequential", singles),
            "concurrent": ("concurrent", singles), "batch-one": ("sequential", [batch_one]),
            "batch-many": ("sequential", [batch_many]),
        }.items()})
    missing = query("missing", '((identifier) @n (#eq? @n "missing"))')
    early = query("early", maxMatches=1)
    long = query("long", "(string) @s", maxMatches=200)
    for scope, spec in [("wide", missing), ("large", missing), ("dense", early), ("long", long)]:
        singles, batch_one, _ = series(scope, [spec])
        cases[f"{scope}.single"] = "sequential", singles
        cases[f"{scope}.batch"] = "sequential", [batch_one]
    cases["dense.batch-full"] = "sequential", [series("dense", [query("dense")])[1]]
    cases["dense.batch-sparse"] = "sequential", [series("dense", [missing])[1]]
    cases["mixed"] = "mixed", [params([missing], paths=["large"], deadlineMs=120000),
                               single(missing, paths=["large"]), single(query("short"), source="short\n")]
    return cases


def data(reply):
    result = reply["result"]
    assert not result.get("isError"), result
    structured = result["structuredContent"]
    assert json.loads(result["content"][0]["text"]) == structured
    return structured


def exchange_once(connection, mode, requests):
    send, receive, _ = connection
    replies = {}
    started = {}
    begin = time.perf_counter_ns()
    for index, spec in enumerate(requests):
        if mode == "mixed" and index == 2:
            # Let the competing requests enter the pool before injecting the short search.
            time.sleep(0.005)
        sent_at = time.perf_counter_ns()
        identifier = send(METHOD, spec)
        started[identifier] = sent_at
        if mode == "sequential":
            reply, size = receive()
            assert reply["id"] == identifier, reply
            replies[identifier] = reply, size, time.perf_counter_ns() - sent_at
    cancelled = None
    if mode == "mixed":
        cancelled = list(started)[1]
        send("notifications/cancelled", {"requestId": cancelled}, notify=True)
    if mode != "sequential":
        expected = started.keys() - {cancelled}
        while not expected.issubset(replies):
            reply, size = receive()
            identifier = reply["id"]
            assert identifier in started and identifier not in replies, reply
            replies[identifier] = reply, size, time.perf_counter_ns() - started[identifier]
    elapsed = time.perf_counter_ns() - begin
    if cancelled is not None:
        # Publication and cancellation are serialized on the protocol thread. Its ping reply
        # follows any already-published result, including one behind both expected replies.
        fence = send("ping", {})
        while True:
            reply, _ = receive()
            if reply["id"] == fence:
                assert reply.get("result") == {}, reply
                break
            assert reply["id"] == cancelled and cancelled not in replies, reply
            replies[cancelled] = reply, 0, 0
        if cancelled in replies:
            for reply, _, _ in replies.values():
                data(reply)
            return None
    ordered = [replies[key] for key in started if key != cancelled]
    sample = {"elapsedNs": elapsed, "wireBytes": sum(size for _, size, _ in ordered),
              "replyNs": [duration for _, _, duration in ordered], "ids": list(started), "cancelledId": cancelled}
    return [data(reply) for reply, _, _ in ordered], sample


def exchange(connection, mode, requests):
    for attempt in range(1, 4):
        result = exchange_once(connection, mode, requests)
        if result is not None:
            if mode == "mixed":
                result[1]["cancelRaceRetries"] = attempt - 1
            return result
    raise RuntimeError("Mixed workload completed before cancellation in three consecutive attempts")


def verify_sparse(case, payloads):
    count, _, mode = case.partition(".")
    if not count.isdigit():
        return
    payloads = payloads[0]["items"] if mode.startswith("batch-") else payloads
    for i, payload in enumerate(payloads):
        text = f"hit{i}"
        assert not (payload["truncated"] or payload["incomplete"] or payload["hasSyntaxErrors"])
        assert payload["matches"] == [{"path": f"{count}/0/{i}.py", "patternIndex": 0,
                                       "capturesTruncated": False, "captures": [{
                                           "name": "n", "text": text, "textTruncated": False,
                                           "range": {"startByte": 0, "endByte": len(text), "startRow": 0,
                                                     "endRow": 0, "startColumn": 0, "endColumn": len(text)},
                                       }]}], payload


def summary(values):
    return {"median": statistics.median(values), "min": min(values), "max": max(values),
            "p95": sorted(values)[math.ceil(0.95 * len(values)) - 1] if len(values) >= 40 else None}


def regressions(cases, args):
    def check(name, workers, metric, baseline, candidate, limit):
        if candidate > baseline * limit:
            return f"{name} at {workers} workers, {metric} ratio {candidate / baseline:.3f} exceeds {limit:.3f}"
        return None

    failures = []
    for name, group in cases.items():
        configurations = {(entry["workers"], entry["binary"]): entry for entry in group["configurations"]}
        for workers in args.workers:
            baseline, candidate = configurations[workers, 0], configurations[workers, 1]
            if args.max_median_ratio and not name.endswith(".tail"):
                failures.append(check(name, workers, "median", baseline["elapsedNs"]["median"],
                                      candidate["elapsedNs"]["median"], args.max_median_ratio))
            if args.max_short_p95_ratio and name == "mixed.tail":
                failures.append(check(name, workers, "short p95", baseline["shortNs"]["p95"],
                                      candidate["shortNs"]["p95"], args.max_short_p95_ratio))
            if args.max_memory_ratio:
                peak = [max(s["peakResidentBytes"] for s in entry["samples"]) for entry in (baseline, candidate)]
                failures.append(check(name, workers, "peak resident memory", *peak, args.max_memory_ratio))
    return [failure for failure in failures if failure]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", action="append", type=lambda value: Path(value).resolve(), required=True,
                        help="First binary supplies the one-worker oracle. Additional binaries are interleaved.")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--tail-repeats", type=int, default=0, help="Separate tail-latency series, at least 40.")
    parser.add_argument("--tail-case", action="append", choices=workloads().keys(), help="Defaults to the mixed workload.")
    parser.add_argument("--case", action="append", choices=workloads().keys())
    parser.add_argument("--workers", type=int, nargs="+", default=WORKERS, choices=WORKERS)
    parser.add_argument("--metadata", type=Path, help="JSON with revisions, build commands, Qt, compiler and storage.")
    parser.add_argument("--resources", action="store_true", help="Sample process memory and handle/descriptor counts.")
    parser.add_argument("--profiles", type=Path, help="New directory for per-process output from profiled builds.")
    parser.add_argument("--batch-buffer", action="append", type=int, metavar="MATCHES",
                        help="Override the batch collector size in a profiled build. Set once per binary.")
    parser.add_argument("--max-median-ratio", type=float)
    parser.add_argument("--max-short-p95-ratio", type=float)
    parser.add_argument("--max-memory-ratio", type=float)
    args = parser.parse_args()
    if args.warmups < 0 or args.repeats < 1 or 0 < args.tail_repeats < 40:
        parser.error("use nonnegative warmups, positive repeats and at least 40 tail repeats")
    if args.batch_buffer and (len(args.batch_buffer) != len(args.binary) or
                              any(not 1 <= size <= 64 for size in args.batch_buffer)):
        parser.error("use one batch buffer size in the range 1–64 per binary")
    gates = [value for value in (args.max_median_ratio, args.max_short_p95_ratio, args.max_memory_ratio)
             if value is not None]
    if gates and (len(args.binary) != 2 or any(not math.isfinite(value) or value <= 0 for value in gates)):
        parser.error("gates require exactly two binaries and positive finite ratios")
    tail_cases = (args.tail_case or ["mixed"]) if args.tail_repeats else []
    if args.tail_case and not args.tail_repeats:
        parser.error("--tail-case requires --tail-repeats")
    if args.max_short_p95_ratio and "mixed" not in tail_cases:
        parser.error("the short p95 gate requires a mixed tail series")
    if args.max_memory_ratio and not args.resources:
        parser.error("the memory gate requires --resources")
    cases = workloads()
    selected = args.case or list(cases)
    configurations = [(i, w) for w in args.workers for i in range(len(args.binary))]
    if args.profiles:
        args.profiles = args.profiles.resolve()
        args.profiles.mkdir()
        os.environ["TREEMCP_SEARCH_PROFILE_DIR"] = str(args.profiles)
    environments = [dict(os.environ, TREEMCP_BATCH_BUFFER_MATCHES=str(size)) for size in args.batch_buffer or ()]
    output = {"command": sys.argv, "platform": platform.platform(), "machine": platform.machine(),
              "processor": platform.processor(), "logicalCpus": os.cpu_count(), "tempDirectory": tempfile.gettempdir(),
              "cache": "natural OS cache with explicit warmups; cache is not cleared", "corpusVersion": CORPUS_VERSION,
              "binaries": [dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                           for path in args.binary], "warmups": args.warmups, "repeats": args.repeats,
              "tailRepeats": args.tail_repeats, "metadata": json.loads(args.metadata.read_text(encoding="utf-8-sig"))
              if args.metadata else {}, "batchBuffers": args.batch_buffer, "cases": {}}
    with tempfile.TemporaryDirectory(prefix="treemcp-speed-") as directory:
        root = Path(directory)
        output["corpusSha256"] = generate(root)
        assert output["corpusSha256"] == CORPUS_HASHES, "Corpus changed without updating its version and manifest"
        runs = [(name, name, args.repeats) for name in selected]
        runs += [(name + ".tail", name, args.tail_repeats) for name in tail_cases]
        for label, case, repeats in runs:
            mode, specs = cases[case]
            print(label, flush=True)
            with wire_session(args.binary[0], root, "--workers", "1", env=environments[0] if environments else None) as connection:
                oracle, _ = exchange(connection, mode, specs)
                verify_sparse(case, oracle)
            samples = {config: [] for config in configurations}
            with ExitStack() as stack:
                pids = {}
                connections = {config: stack.enter_context(wire_session(
                    args.binary[config[0]], root, "--workers", str(config[1]),
                    env=environments[config[0]] if environments else None,
                    process=lambda child, config=config: pids.update({config: child.pid}))) for config in configurations}
                for iteration in range(args.warmups + repeats):
                    offset = iteration % len(configurations)
                    for config in configurations[offset:] + configurations[:offset]:
                        payload, sample = measured_exchange(connections[config], mode, specs, pids[config], args.resources)
                        assert payload == oracle, (label, config, "response differs from the one-worker baseline")
                        file_count = {"1000": 1000, "10000": 10000, "large": 8, "wide": 128,
                                      "dense": 1000, "long": 8, "mixed": 8}[case.split(".")[0]]
                        if case not in {"dense.single", "long.single"}:
                            traversals = len(specs) if case.endswith((".sequential", ".concurrent")) else 1
                            sample["filesPerSecond"] = file_count * traversals * 1e9 / sample["elapsedNs"]
                        if iteration >= args.warmups:
                            samples[config].append(sample)
            output["cases"][label] = {"requests": specs, "oracle": oracle, "configurations": [
                {"binary": index, "workers": workers, "samples": values,
                 "elapsedNs": summary([sample["elapsedNs"] for sample in values]),
                 **({"shortNs": summary([sample["replyNs"][-1] for sample in values])} if mode == "mixed" else {})}
                for (index, workers), values in samples.items()]}
            args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
        for binary in output["binaries"]:
            times = []
            for _ in range(args.warmups + args.repeats):
                started = time.perf_counter_ns()
                subprocess.run([binary["path"], "--version"], capture_output=True, check=True, timeout=10)
                times.append(time.perf_counter_ns() - started)
            binary["cliStartup"] = {"samplesNs": times[args.warmups:], **summary(times[args.warmups:])}
    failures = regressions(output["cases"], args) if gates else []
    if gates:
        output["regressionFailures"] = failures
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    if failures:
        raise SystemExit("\n".join(failures))


if __name__ == "__main__":
    main()
