"""Exercise mixed benchmark samples with both possible cancellation outcomes."""

from collections import deque
import json
import os
from pathlib import Path
from runpy import run_path
import sys
from tempfile import TemporaryDirectory

from check_workspace import wire_session


exchange = run_path(str(Path(__file__).resolve().parents[1] / "scripts/benchmark-search.py"))["exchange"]


def check_mixed(outcomes):
    replies = deque()
    ids = []
    result = {"structuredContent": {}, "content": [{"text": "{}"}]}
    next_id = 0
    attempts = iter(outcomes)

    def send(method, params, notify=False):
        nonlocal next_id
        if notify:
            assert method == "notifications/cancelled" and params["requestId"] == ids[1]
            for index in next(attempts):
                replies.append({"id": ids[index], "result": result})
            ids.clear()
            return None
        next_id += 1
        if method == "ping":
            replies.append({"id": next_id, "result": {}})
        else:
            ids.append(next_id)
        return next_id

    def receive():
        return replies.popleft(), 10

    try:
        payload, sample = exchange((send, receive, None), "mixed", [{}, {}, {}])
    finally:
        assert not replies and not ids
    assert payload == [{}, {}] and sample["wireBytes"] == 20
    assert sample["cancelRaceRetries"] == len(outcomes) - 1


def check_retries():
    check_mixed([[0, 2]])
    check_mixed([[1, 0, 2], [0, 2, 1], [2, 0]])
    try:
        check_mixed([[0, 1, 2]] * 3)
    except RuntimeError as error:
        assert "three consecutive attempts" in str(error)
    else:
        raise AssertionError("Expected bounded retries after repeated publication before cancellation")


def check_serialization(executable):
    with TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "one.py").write_text("alpha\n", encoding="utf-8")
        spec = {"language": "python", "query": "(identifier) @name"}
        requests = [
            {"name": "structural_search", "arguments": {**spec, "source": "alpha\n"}},
            {"name": "batch_search", "arguments": {"paths": ["one.py"], "queries": [{**spec, "id": "q"}]}},
        ]
        sizes = {}
        env = dict(os.environ, TREEMCP_SEARCH_PROFILE_DIR=str(root))
        with wire_session(executable, root, env=env) as (send, receive, _):
            for request in requests:
                ident = send("tools/call", request)
                reply, sizes[ident] = receive()
                assert reply["id"] == ident and "structuredContent" in reply["result"]
        events = [json.loads(line) for f in root.glob("*.jsonl") for line in f.read_text().splitlines()]
        # Single encodes the entry, text content and size check. Batch encodes the empty
        # envelope twice, the entry and its quoted text once each, then the final text content.
        for ident, expected_calls in zip(sizes, (3, 5)):
            finished, = [r for r in events if r["id"] == ident and r["event"] == "finished"]
            wire, = [r for r in events if r["id"] == ident and r["event"] == "wireSerialize"]
            assert finished["phases"]["serialize"]["calls"] == expected_calls, finished
            assert wire["bytes"] == sizes[ident] and wire["ns"] >= 0, wire


if __name__ == "__main__":
    if len(sys.argv) == 2:
        check_serialization(Path(sys.argv[1]).resolve())
    else:
        check_retries()
