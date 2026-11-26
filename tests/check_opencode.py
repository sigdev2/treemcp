"""Exercise the shipped skill through OpenCode's standalone direct-tool CLI."""

import argparse
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shlex
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
INTEGRATION = ROOT / "integrations/opencode-v2"
SKILL = "structural-search"
REFERENCE = f".opencode/skills/{SKILL}/references/cli-call.md"


def scenario(cli, workspace, env, prompt, *, fallback=False):
    run = subprocess.run([cli, "run", "--standalone", "--model", "fixture/agent", "--format", "json",
                          "Load the structural-search skill once, then follow its instructions. " + prompt],
                         cwd=workspace, env=env, capture_output=True, text=True, encoding="utf-8", timeout=420)
    if run.returncode:
        raise RuntimeError(run.stderr or run.stdout)
    events = [json.loads(line) for line in run.stdout.splitlines()]
    calls = [e["part"] for e in events if e["type"] == "tool_use"]
    answer = "\n".join(e["part"]["text"] for e in events if e["type"] == "text")
    skill_file = workspace / ".opencode/skills" / SKILL / "SKILL.md"
    body = skill_file.read_text(encoding="utf-8").split("---\n", 2)[2].strip()
    assert any(call["tool"] == "skill" and call["state"]["input"] == {"id": SKILL}
               and call["state"]["status"] == "completed" and body in call["state"]["output"].replace("\r\n", "\n")
               for call in calls), (calls, answer)
    allowed = {"skill", "read", "shell"} if fallback else {"skill"}
    assert all(c["tool"] in allowed or (not fallback and c["tool"].startswith("treemcp_"))
               for c in calls), calls
    print([(c["tool"], c["state"]["status"]) for c in calls], answer, flush=True)
    return calls, answer


def result(calls, name):
    states = [c["state"] for c in calls if c["tool"] == "treemcp_" + name]
    completed = [s for s in states if s["status"] == "completed"]
    assert len(completed) == 1, (name, states)
    return json.loads(completed[0]["output"])


def exercise(cli, workspace, env, config):
    calls, answer = scenario(cli, workspace, env, "Find the Python function defined in src/worker.py. "
                             "Give its name, path and the capture's startRow.")
    match, = result(calls, "structural_search")["matches"]
    assert match["path"] == "src/worker.py" and "src/worker.py" in answer and "refresh" in answer, answer
    assert match["captures"][0]["range"]["startRow"] == 1 and "1" in answer, (match, answer)
    assert all(c["tool"] != "treemcp_batch_search" for c in calls), calls

    calls, answer = scenario(cli, workspace, env, "Find Python function and class definitions in src, "
                             "and give their names in two groups.")
    batch = result(calls, "batch_search")
    assert len(batch["items"]) == 2 and len({item["id"] for item in batch["items"]}) == 2, batch
    assert batch["stats"]["filesRead"] == batch["stats"]["treeBuilds"] == 2, batch
    assert all(c["tool"] != "treemcp_structural_search" for c in calls), calls
    assert all(name in answer for name in ("refresh", "dispatch", "Worker")), answer

    calls, answer = scenario(cli, workspace, env, "List registered languages. Inspect the root syntax node of "
                             "src/value.h explicitly as C. Validate the C query (missing_node) verbatim "
                             "without correcting it. Report the root type and validation error.")
    assert len(result(calls, "languages")["languages"]) == 9
    assert result(calls, "syntax_tree")["nodes"][0]["type"] == "translation_unit"
    assert not result(calls, "query_validate")["valid"]

    calls, answer = scenario(cli, workspace, env, 'Search src with a single batch using exactly these two Python '
                             'queries: id names, (identifier) @name, maxMatches 1; id invalid, (missing_node). '
                             'Keep both queries unchanged and explain coverage and errors.')
    batch = result(calls, "batch_search")
    items = {i["id"]: i for i in batch["items"]}
    assert items["names"]["truncated"] and items["invalid"]["error"] and batch["incomplete"], batch
    assert any(s in answer.lower() for s in ("truncat", "limit", "partial", "omitted")), answer
    assert any(s in answer.lower() for s in ("invalid", "error", "unknown")), answer

    calls, answer = scenario(cli, workspace, env, "Inspect ../outside.py with syntax_tree as Python. "
                             "Try the supplied path once and report the outcome.")
    assert any(c["tool"] == "treemcp_syntax_tree" and c["state"]["status"] == "error" for c in calls), calls

    exercise_fallback(cli, workspace, env, config)


def fallback_calls(cli, workspace, env, config):
    config["mcp"]["servers"]["treemcp"]["disabled"] = True
    executable = shlex.quote(Path(env["TREEMCP_EXECUTABLE"]).as_posix())
    command = f"{executable} --workspace-root . call structural_search --args"
    fallback_config = {**config, "permissions": [
        *config["permissions"],
        {"action": "read", "resource": REFERENCE, "effect": "allow"},
        {"action": "shell", "resource": f"{command} *", "effect": "allow"},
    ]}
    (workspace / "opencode.json").write_text(json.dumps(fallback_config), encoding="utf-8")

    prompt = ("The TreeMCP MCP server is disabled. Its configuration uses the project root and the default "
              "directory exclusions. Read the skill's CLI reference. To find the Python function defined "
              "in src/worker.py, make one shell call using this exact prefix and append the single-quoted "
              f"JSON argument: {command}\n"
              "Report the captured name and path on success. If the executable cannot be launched, "
              "report the shell diagnostic and stop.")
    calls, answer = scenario(cli, workspace, env, prompt, fallback=True)
    reference_index = next((i for i, call in enumerate(calls) if call["tool"] == "read"
                            and call["state"]["status"] == "completed"
                            and PureWindowsPath(call["state"]["input"]["path"]).as_posix().endswith(REFERENCE)), None)
    assert reference_index is not None, [(c["tool"], c["state"]["input"]) for c in calls]
    output = [c["state"]["output"] for c in calls[reference_index + 1:] if c["tool"] == "shell"
              and c["state"]["status"] == "completed" and c["state"]["input"]["command"].startswith(command + " ")]
    assert output, calls
    return output, answer


def exercise_fallback(cli, workspace, env, config):
    output, answer = fallback_calls(cli, workspace, env, config)
    assert any('"text":"refresh"' in out and '"path":"src/worker.py"' in out for out in output), output
    assert "refresh" in answer and "src/worker.py" in answer, answer

    missing_executable = workspace / "missing-treemcp"
    missing = dict(env, TREEMCP_EXECUTABLE=str(missing_executable))
    output, answer = fallback_calls(cli, workspace, missing, config)
    launch_error = re.compile(r"no such file or directory|command not found|is not recognized", re.IGNORECASE)
    assert any(missing_executable.name in line and launch_error.search(line)
               for out in output for line in out.splitlines()), output
    assert any(s in answer.lower() for s in ("unavailable", "unable", "cannot", "could not", "can't",
                                             "not available", "not found", "couldn't", "missing",
                                             "failed", "does not exist")), answer
    assert "refresh" not in answer, answer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("--client", default="opencode")
    parser.add_argument("--base-url", required=True, help="Local OpenAI-compatible chat endpoint including /v1")
    parser.add_argument("--model", required=True, help="Tool-capable model ID at that endpoint")
    args = parser.parse_args()
    cli = shutil.which(args.client) or args.client
    version = subprocess.check_output([cli, "--version"], text=True).strip()
    if version != "opencode v2.1.4":
        raise RuntimeError(f"Expected OpenCode v2.1.4, found {version}")
    if not __debug__:
        raise RuntimeError("The checks require Python assertions")
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        workspace = root / "workspace"
        shutil.copytree(INTEGRATION, workspace)
        shutil.copytree(ROOT / "tests/fixtures/opencode/src", workspace / "src")
        subprocess.run(["git", "init", "-q"], cwd=workspace, check=True)
        env = dict(os.environ, TREEMCP_EXECUTABLE=str(args.server.resolve()), OPENCODE_TEST_HOME=str(root / "home"))
        for key in ("CONFIG", "DATA", "CACHE", "STATE"):
            env[f"XDG_{key}_HOME"] = str(root / key.lower())
        config = json.loads((workspace / "opencode.json").read_bytes())
        config["providers"] = {"fixture": {
            "package": "@opencode/ai/providers/openai-compatible",
            "settings": {"baseURL": args.base_url, "apiKey": "local-fixture"},
            "models": {"agent": {"modelID": args.model, "limit": {"context": 16384, "output": 1024}}},
        }}
        config["agents"] = {"build": {"steps": 10}}
        config["model"] = "fixture/agent"
        config["permissions"] = [
            {"action": "*", "resource": "*", "effect": "deny"},
            {"action": "skill", "resource": SKILL, "effect": "allow"},
            {"action": "treemcp_*", "resource": "*", "effect": "allow"},
        ]
        (workspace / "opencode.json").write_text(json.dumps(config), encoding="utf-8")
        exercise(cli, workspace, env, config)


if __name__ == "__main__":
    main()
