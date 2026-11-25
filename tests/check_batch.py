"""Exercise the batch contract against the real server and independent single searches."""

import json
import os
from pathlib import Path
import sys
import tempfile
import time

from check_workspace import session, wire_session, write


def query(item_id, text="(identifier) @name", language="python", **limits):
    return dict(id=item_id, language=language, query=text, **limits)


def params(queries, **scope):
    return {"name": "batch_search", "arguments": {"paths": ["."], "queries": queries, **scope}}


def batch(call, queries, **scope):
    return call("batch_search", **params(queries, **scope)["arguments"])


def complete(data):
    assert not data["incomplete"] and not data["truncated"] and not data["deadlineExceeded"], data
    return data["items"]


def equivalent(call, queries, paths, expected_hits=None, **limits):
    data = batch(call, queries, paths=paths, deadlineMs=120000, **limits)
    results = complete(data)
    assert [r["id"] for r in results] == [q["id"] for q in queries]
    for item, spec in zip(results, queries):
        if expected_hits is not None:
            path, text = expected_hits[spec["id"]]
            match, = item["matches"]
            capture, = match["captures"]
            assert match["path"] == path and match["patternIndex"] == 0 and not match["capturesTruncated"], match
            assert capture == dict(name="n", text=text, textTruncated=False,
                                   range=dict(startByte=0, endByte=len(text), startRow=0, endRow=0,
                                              startColumn=0, endColumn=len(text))), capture
            assert not item["hasSyntaxErrors"] and not item["incomplete"] and not item["truncated"], item
        single = call(paths=paths, **{k: v for k, v in spec.items() if k != "id"})
        assert not (single["truncated"] or single["incomplete"])
        assert item["hasSyntaxErrors"] == single["hasSyntaxErrors"]
        # Compare complete match multisets, preserving capture order and duplicate matches.
        def canonical(matches):
            return sorted(json.dumps(m, sort_keys=True) for m in matches)
        assert canonical(item["matches"]) == canonical(single["matches"])
    return data["stats"]


def check_limits(call):
    q = query("one")
    invalid = [
        {"queries": []}, {"queries": [q] * 2}, {"queries": [None]}, {"queries": ["x"]},
        {"queries": [dict(q, paths=["."])]}, {"queries": [{"id": "x", "query": "(_)"}]},
        {"queries": [dict(q, id="")]}, {"queries": [dict(q, id="a" * 129)]},
        {"queries": [dict(q, id=str(i)) for i in range(17)]},
        {"queries": [dict(q, id=str(i), query=";" + "a" * 65535) for i in range(5)]},
        {"queries": [dict(q, maxMatches=0)]}, {"maxTotalMatches": 201}, {"maxFiles": 10001},
        {"deadlineMs": 0}, {"maxResponseBytes": 131071}, {"sort": "discovery"},
        {"paths": ["src", "../bad"]}, {"exclude": ["../bad"]},
        {"queries": [dict(q, language="unknown")], "include": ["../bad"]},
    ]
    for args in invalid:
        result = call("batch_search", **{"queries": [q], "paths": ["."], **args})
        assert result.get("isError"), args


def check_search(executable, root):
    paths = ["src", "src/nested", "src/z.py", "src"]
    queries = [query("names"), query("second", "((identifier) @n (#eq? @n \"target\"))"),
               query("cpp", "(identifier) @name", "cpp"), query("absent", "(class_definition) @c")]
    write(root, "src/z.py", "target = 1\n")
    write(root, "src/nested/a.py", "é = target\n")
    write(root, "src/b.cpp", "int main() {}")
    write(root, "src/.gitignore", "skip.py\n")
    write(root, "src/skip.py", "skip")
    write(root, "src/blob.py", b"\0")
    with session(executable, root) as call:
        check_limits(call)
        write(root, "mixed/small.py", "visible")
        with write(root, "mixed/large.cpp", "").open("wb") as output:
            output.truncate(8 * 1024 * 1024 + 1)
        data = batch(call, [query("python"), query("cpp1", language="cpp"), query("cpp2", language="cpp")],
                     paths=["mixed"])
        assert data["incomplete"] and [d["path"] for d in data["diagnostics"]] == ["mixed/large.cpp"], data
        py, *cpp = data["items"]
        single = call(paths=["mixed"])
        assert not single["incomplete"] and not py["incomplete"] and py["matches"] == single["matches"], data
        assert len(py["matches"]) == 1 and all(item["incomplete"] and not item["matches"] for item in cpp), data
        stats = equivalent(call, queries, paths)
        assert stats == dict(filesVisited=4, filesRead=4, filesParsed=3, treeBuilds=3, queryExecutions=7), stats
        bad = [query("language", "(_)", "missing"), query("syntax", "(missing_type)"), query("empty", ""),
               query("predicate", '((identifier) @i (#match? @i "x"))')]
        data = batch(call, [queries[0], *bad, queries[-1]], paths=paths)
        assert data["incomplete"] and not data["truncated"]
        good, *failed, empty = data["items"]
        assert len(good["matches"]) == 3 and not good["incomplete"]
        assert all(item["error"]["message"] and item["incomplete"] and not item["matches"] for item in failed)
        assert all("byteOffset" in item["error"] for item in failed[1:])
        assert not empty["incomplete"] and not empty["truncated"] and empty["matches"] == []
        assert batch(call, bad)["stats"]["filesVisited"] == 0

        data = batch(call, queries, paths=paths, maxFiles=1)
        assert data["incomplete"] and data["stats"]["filesVisited"] == 1 and data["diagnostics"]
        data = batch(call, queries, paths=["src/nested", "missing"], exclude=["**/z.py"])
        assert data["incomplete"] and data["diagnostics"] and data["items"][0]["matches"]
        for value in ["fresh", "changed"]:
            write(root, "src/z.py", value)
            item, = complete(batch(call, [queries[0]], paths=["src/z.py"]))
            assert [m["captures"][0]["text"] for m in item["matches"]] == [value]
        (root / "src/z.py").unlink()
        item, = complete(batch(call, [queries[0]], paths=["src"], include=["z.py"]))
        assert not item["matches"]
        if os.name != "nt":
            # The child runs in root.parent. Remove write permission on its cwd and the full scope.
            files = [p for p in root.rglob("*") if p.is_file()]
            directories = [root.parent, root, *(p for p in root.rglob("*") if p.is_dir())]
            for p in files:
                p.chmod(0o444)
            for p in directories:
                p.chmod(0o555)
            try:
                stats = equivalent(call, queries, ["src"])
                assert stats["filesParsed"] == 2
                assert set(root.rglob("*")) == set(files + directories[2:])
            finally:
                for p in directories:
                    p.chmod(0o755)
                for p in files:
                    p.chmod(0o644)


def check_order(executable, root):
    paths = ["order"]
    names = ["z", "\U0001f600", "\ue000", "b", "a", "é"]
    queries = [query("first", "(identifier) @x\n(identifier) @y", maxMatches=7),
               query("second"), query("third")]
    previous = None
    for workers in (1, 2, 4, 8):
        # Reverse fixture creation order between runs and vary per-file parse cost.
        names.reverse()
        for name in names:
            write(root, f"order/{name}.py", "a = b\n#" + "x" * (1000 * ord(name[0]) % 100000))
        with session(executable, root, "--batch-workers", str(workers)) as call:
            data = batch(call, queries, paths=paths, maxTotalMatches=9)
            assert data["truncated"] and not data["incomplete"]
            items = data["items"]
            assert [len(i["matches"]) for i in items] == [7, 2, 0] and all(i["truncated"] for i in items)
            keys = [(m["path"], m["captures"][0]["range"]["startByte"], m["patternIndex"])
                    for m in items[0]["matches"]]
            assert keys == [("order/a.py", 0, 0), ("order/a.py", 0, 1), ("order/a.py", 4, 0),
                            ("order/a.py", 4, 1), ("order/b.py", 0, 0), ("order/b.py", 0, 1),
                            ("order/b.py", 4, 0)], keys
            assert [(m["path"], m["captures"][0]["text"]) for m in items[1]["matches"]] == [
                ("order/a.py", "a"), ("order/a.py", "b")]
            assert previous is None or previous == data
            previous = data
        for name in names:
            (root / f"order/{name}.py").unlink()


def check_item_limits(executable, root):
    write(root, "ties.py", "f(a,b,c,d)")
    write(root, "repeat.py", "x\n" * 11000 + "winner")
    tie = "(argument_list (identifier) @first (identifier) @second)"
    repeated = "(module (expression_statement (identifier) @n)+)"
    with session(executable, root) as call:
        result = batch(call, [query("ties", tie, maxMatches=4)], paths=["ties.py"])
        item, = result["items"]
        assert not result["incomplete"] and result["truncated"] and item["truncated"], result
        assert [[c["text"] for c in m["captures"]] for m in item["matches"]] == [
            ["a", "b"], ["a", "c"], ["a", "d"], ["b", "c"]]
        result = batch(call, [query("repeat", repeated), query("literal", '((identifier) @n (#eq? @n "winner"))')],
                       paths=["repeat.py"])
        failed, good = result["items"]
        assert result["incomplete"] and not result["truncated"] and not result["deadlineExceeded"], result
        assert failed["incomplete"] and not failed["matches"] and not good["incomplete"], result
        match, = good["matches"]
        capture, = match["captures"]
        assert capture["text"] == "winner" and match["path"] == "repeat.py", match
        diagnostic = dict(path="repeat.py", message="Source tree exceeds the node limit for repeated captures")
        assert result["diagnostics"] == [diagnostic], result
        single = call(paths=["repeat.py"], query=repeated)
        assert single["incomplete"] and not single["matches"] and single["diagnostics"] == [diagnostic], single


def check_wire(executable, root):
    write(root, "text.py", ("'" + '\\"' * 550 + "'\n") * 200)
    write(root, "heavy/task.py", "a=b\n" * 250000)
    write(root, "quick.py", "quick")
    heavy = params([query(str(i)) for i in range(16)], paths=["heavy"])
    with wire_session(executable, root) as (send, receive, request):
        sent = send("tools/call", params([query("strings", "(string) @s", maxMatches=200)],
                                         paths=["text.py"], maxResponseBytes=131072))
        reply, size = receive()
        assert reply["id"] == sent and size <= 131072
        data = reply["result"]["structuredContent"]
        assert json.loads(reply["result"]["content"][0]["text"]) == data
        assert data["truncated"] and not data["incomplete"]
        assert 0 < len(data["items"][0]["matches"]) < 200
        data = request("tools/call", params([query("deadline")], paths=["heavy"], deadlineMs=5))["structuredContent"]
        assert data["incomplete"] and data["deadlineExceeded"] and data["items"][0]["incomplete"]
        active = send("tools/call", heavy)
        assert request("ping", {}) == {}
        small = request("tools/call", {"name": "structural_search", "arguments": {
            "language": "python", "query": "(identifier) @n", "paths": ["quick.py"]}})
        assert small["structuredContent"]["matches"][0]["captures"][0]["text"] == "quick"
        other = send("tools/call", heavy)
        busy, _ = receive()
        assert busy["id"] == other and busy["error"]["code"] == -32001
        send("notifications/cancelled", {"requestId": active}, notify=True)
        end = time.monotonic() + 5
        while time.monotonic() < end:
            req = send("tools/call", params([query("small")], paths=["quick.py"]))
            reply, _ = receive()
            assert req == reply["id"], "Cancelled batch published a response"
            if "result" in reply:
                assert complete(reply["result"]["structuredContent"])[0]["matches"]
                break
            assert reply["error"]["code"] == -32001
            time.sleep(0.01)
        else:
            raise AssertionError("Cancelled batch did not release its workers")
        send("tools/call", heavy)  # Closing stdin cancels and joins this batch.


def corpus(executable, root):
    for count, query_count in [(1000, 16), (10000, 2)]:
        scope = root / str(count)
        scope.mkdir()
        for i in range(count):
            # Sparse, distinct hits allow exact comparison with unsorted single searches under their limits.
            folder = scope / str(i // 100)
            if i % 100 == 0:
                folder.mkdir()
            (folder / f"{i}.py").write_bytes(f"hit{i}\n".encode() if i < query_count else b"pass\n")
        queries = [query(str(i), f'((identifier) @n (#eq? @n "hit{i}"))') for i in range(query_count)]
        with session(executable, root) as call:
            stats = equivalent(call, queries, [scope.name, f"{scope.name}/0", scope.name],
                               {str(i): (f"{count}/0/{i}.py", f"hit{i}") for i in range(query_count)})
            assert stats == dict(filesVisited=count, filesRead=count, filesParsed=count,
                                 treeBuilds=count, queryExecutions=count * query_count)


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "workspace"
        root.mkdir()
        if sys.argv[2] == "corpus":
            corpus(executable, root)
        else:
            check_search(executable, root)
            check_order(executable, root)
            check_item_limits(executable, root)
            check_wire(executable, root)
