"""Exercise search admission, cancellation and deadlines through a live MCP connection."""

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from check_batch import params, query
from check_workspace import session, wire_session, write


def search(**args):
    return {"name": "structural_search", "arguments": {
        "language": "python", "query": "(identifier) @name", **args}}


def cancel(send, request_id):
    send("notifications/cancelled", {"requestId": request_id}, notify=True)


def check_deadlines(executable, root):
    with wire_session(executable, root, "--workers", "1") as (send, receive, request):
        # A UTF-8 identifier straddles the parser's 64 KiB input checkpoint.
        prefix = "#" + "x" * 65533 + "\n"
        content = request("tools/call", search(source=prefix + "\u03c0 = 1"))["structuredContent"]
        capture = content["matches"][0]["captures"][0]
        assert capture["text"] == "\u03c0" and capture["range"]["startByte"] == 65535
        for name, arguments in [("structural_search", search(path="big.py")["arguments"]),
                                ("syntax_tree", {"language": "python", "path": "big.py"})]:
            result = request("tools/call", {"name": name, "arguments": {**arguments, "deadlineMs": 5}})
            assert result["isError"] and "deadline" in result["content"][0]["text"]

        running = send("tools/call", search(path="big.py"))
        result = request("tools/call", {"name": "query_validate", "arguments": {
            "language": "cpp", "query": "(identifier)", "deadlineMs": 20}})
        assert result["isError"] and "deadline" in result["content"][0]["text"]
        queued = send("tools/call", search(source="cancel_me"))
        cancel(send, queued)
        cancel(send, running)
        reply = request("tools/call", search(source="survivor"))
        assert reply["structuredContent"]["matches"][0]["captures"][0]["text"] == "survivor"
        # Closing the input with both kinds of work active must join within wire_session's grace interval.
        send("tools/call", search(path="big.py"))
        send("tools/call", params([query("batch")], paths=["big.py"]))
        for _ in range(8):
            send("tools/call", search(path="big.py"))
        assert request("ping", {}) == {}


def check_capacity(executable, root, weighted):
    with wire_session(executable, root, "--workers", "1") as (send, receive, request):
        pending = [send("tools/call", search(path="big.py"))]
        if weighted:
            pending.append(send("tools/call", params([query(str(i)) for i in range(16)], paths=["big.py"])))
            # Capacity is 132 units. The running call and batch use 21, leaving room for 27 scans.
            for _ in range(27):
                pending.append(send("tools/call", search(paths=["big.py"])))
            extra = search(paths=["big.py"])
        else:
            for _ in range(32):
                pending.append(send("tools/call", search(source="queued")))
            extra = search(source="overloaded")
        rejected = send("tools/call", extra)
        reply, _ = receive()
        assert reply["id"] == rejected and reply["error"]["code"] == -32001, reply
        assert request("ping", {}) == {}
        for request_id in reversed(pending):
            cancel(send, request_id)
        reply = request("tools/call", search(source="again"))
        assert reply["structuredContent"]["matches"][0]["captures"][0]["text"] == "again"


def check_races(executable, root):
    with wire_session(executable, root) as (send, receive, request):
        for i in range(60):
            current = send("tools/call", search(source="racing"))
            cancel(send, current)
            fence = send("ping", {})
            reply, _ = receive()
            if reply["id"] == current:  # Completion won before the notification was dispatched.
                assert reply["result"]["structuredContent"]["matches"]
                reply, _ = receive()
            assert reply["id"] == fence, (i, reply)
            cancel(send, current)  # A late or repeated notification cannot cancel the next request.
        assert request("tools/call", search(source="last"))["structuredContent"]["matches"]


def check_output_backpressure(executable, root):
    # A disconnected reader must wake the server even while the input pipe remains open.
    with subprocess.Popen([executable, "--workspace-root", root], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
        try:
            child.stdout.close()
            child.stdin.write(b'{"jsonrpc":"2.0","id":1,"method":"ping"}\n')
            child.stdin.flush()
            assert child.wait(timeout=5) == 1
            assert b"stdio" in child.stderr.read()
        finally:
            child.kill()

    with wire_session(executable, root, "--workers", "1") as (send, receive, _):
        # Leave catalog replies unread while the worker is occupied. The short deadline
        # must publish before the later cancellation, even though stdout is still full.
        catalogs = [send("tools/list", {}) for _ in range(96)]
        running = send("tools/call", search(path="big.py"))
        queued = send("tools/call", search(source="queued", deadlineMs=20))
        time.sleep(0.25)
        cancel(send, queued)
        cancel(send, running)
        fence = send("ping", {})
        for expected in catalogs:
            reply, _ = receive()
            assert reply["id"] == expected and reply["result"]["tools"], reply
        reply, _ = receive()
        assert reply["id"] == queued and "deadline" in reply["result"]["content"][0]["text"], reply
        assert receive()[0]["id"] == fence

    # Feed a finite stream and wait for process exit while holding the read end open.
    # A stalled final drain is a transport failure and must release both I/O threads.
    with tempfile.TemporaryFile() as frames:
        def frame(method, params, **fields):
            frames.write((json.dumps(dict(jsonrpc="2.0", method=method, params=params, **fields)) + "\n").encode())

        frame("initialize", dict(protocolVersion="2025-11-25", capabilities={},
                                 clientInfo=dict(name="test", version="1")), id=0)
        frame("notifications/initialized", {})
        for i in range(1, 193):
            frame("tools/list", {}, id=i)
        frames.seek(0)
        with subprocess.Popen([executable, "--workspace-root", root], stdin=frames,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE) as child:
            try:
                assert child.wait(timeout=5) == 1
                assert b"stdio" in child.stderr.read()
            finally:
                child.kill()


def check_session_limit(executable, root):
    def ping(request_id):
        return json.dumps(dict(jsonrpc="2.0", method="ping", id=request_id)) + "\n"

    # Exercise the exact admission boundary in one pipe exchange.
    count = 16384
    frames = "".join(ping(i) for i in range(count)) + ping(count) + ping(0)
    child = subprocess.run([executable, "--workspace-root", root], input=frames, capture_output=True,
                           encoding="utf-8", timeout=15, check=True)
    records = child.stdout.splitlines()
    assert len(records) == count + 2 and not child.stderr
    for i, record in enumerate(records[:count]):
        assert json.loads(record) == {"jsonrpc": "2.0", "id": i, "result": {}}, record
    for record, expected_id, code in [(records[-2], count, -32001), (records[-1], 0, -32600)]:
        reply = json.loads(record)
        assert reply["id"] == expected_id and reply["error"]["code"] == code, reply


def check_scoped_prefix(executable, root):
    write(root, "prefix/one.py", "alpha\n")
    write(root, "prefix/two.py", "alpha\nbeta\n")
    write(root, "prefix/long.py", ('x = "' + 'a' * 1400 + '"\n') * 240)
    write(root, "prefix/large.py", b"x" * (8 * 1024 * 1024 + 1))
    paths = ["prefix/one.py", "prefix/two.py", "prefix/large.py"]
    missing = paths + [f"prefix/missing{i:02}.py" for i in reversed(range(20))]
    variants = [dict(paths=[paths[0]], maxMatches=1), dict(paths=[paths[1]], maxMatches=1),
                dict(paths=["prefix/long.py"], query="(string) @s", maxMatches=1),
                dict(paths=["prefix/long.py"], query="(string) @s", maxMatches=240),
                dict(paths=paths, maxMatches=2), dict(paths=missing)]
    with session(executable, root, "--workers", "1") as call:
        oracle = [call(**args) for args in variants]
    exact, overflow, clipped, byte_limited, _, diagnostics = oracle
    assert len(exact["matches"]) == len(overflow["matches"]) == 1
    assert not exact["truncated"] and overflow["truncated"]
    assert clipped["truncated"] and clipped["matches"][0]["captures"][0]["textTruncated"]
    assert byte_limited["truncated"] and 1 < len(byte_limited["matches"]) < 240
    assert diagnostics["incomplete"] and len(diagnostics["diagnostics"]) == 16
    assert [d["path"] for d in diagnostics["diagnostics"]] == paths[2:] + list(reversed(missing[-20:]))[:15]
    for workers in (2, 4, 8):
        with session(executable, root, "--workers", str(workers)) as call:
            assert [call(**args) for args in variants] == oracle


def check_scoped_owners(executable, root):
    write(root, "short/one.py", "survivor\n")
    write(root, "medium.py", b"a=b\n" * 12000)
    with wire_session(executable, root, "--workers", "2") as (send, receive, request):
        batch = send("tools/call", params([query("batch")], paths=["medium.py"]))
        first = send("tools/call", search(paths=["big.py"]))
        second = send("tools/call", search(paths=["short"]))
        cancel(send, first)
        received = {}
        for _ in range(2):
            reply, _ = receive()
            received[reply["id"]] = reply["result"]["structuredContent"]
        assert received.keys() == {batch, second}
        assert received[batch]["items"][0]["matches"]
        assert received[second]["matches"][0]["captures"][0]["text"] == "survivor"

        result = request("tools/call", search(paths=["big.py"], deadlineMs=5))
        assert result["isError"] and "deadline" in result["content"][0]["text"]
        write(root, "short/one.py", "updated\n")
        data = request("tools/call", search(paths=["short"]))["structuredContent"]
        assert data["matches"][0]["captures"][0]["text"] == "updated"
        send("tools/call", params([query("batch")], paths=["big.py"]))
        send("tools/call", search(paths=["big.py"]))
        send("tools/call", search(paths=["big.py"]))
        assert request("ping", {}) == {}  # EOF while both coordinators and the sequential fallback are active.


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as temp:
        workspace = Path(temp) / "workspace"
        write(workspace, "big.py", b"a=b\n" * 450000)
        program = str(Path(sys.argv[1]).resolve())
        check_deadlines(program, workspace)
        for use_cost in (False, True):
            check_capacity(program, workspace, use_cost)
        check_races(program, workspace)
        check_output_backpressure(program, workspace)
        check_session_limit(program, workspace)
        check_scoped_prefix(program, workspace)
        check_scoped_owners(program, workspace)
