# OpenCode v2 integration

The project-local configuration and `structural-search` skill target **OpenCode
2.1.4**, standalone CLI sessions, direct MCP tools and stdio revision
`2025-11-25`. The server runs with the active project directory as its workspace.
[OpenCode 2.1.4 release](https://github.com/anomalyco/opencode/releases/tag/v2.1.4)

## Install in a project

1. Install the pinned CLI with `npm install -g @opencode/cli@2.1.4` and verify
   `opencode --version` reports `opencode v2.1.4`.
2. [Install TreeMCP](../getting-started/deployment.md). Set `TREEMCP_EXECUTABLE`
   to the absolute path of the installed binary in the environment that starts
   OpenCode. Use `export TREEMCP_EXECUTABLE=/opt/treemcp/bin/structural-mcp` on
   Linux or `$env:TREEMCP_EXECUTABLE = 'C:\TreeMCP\bin\structural-mcp.exe'`
   in PowerShell.
3. Copy the [skill directory](../../integrations/opencode-v2/.opencode/skills/structural-search)
   into `.opencode/skills/` in the target project. Its resulting path must be
   `.opencode/skills/structural-search/SKILL.md`. Include its `references/`
   directory. Keep the ID unique across
   project skill sources.
4. Copy the [project config](../../integrations/opencode-v2/opencode.json)
   into the project root. For an existing `opencode.json` or `opencode.jsonc`,
   merge its `mcp.servers.treemcp` entry into the existing document. The entry
   enables direct tools with `codemode: false`. The command array keeps paths
   with spaces in one argument. The explicit `cwd` and `--workspace-root`
   bind the child process to the active workspace.
5. From the target project, run a session using a configured tool-capable model.

```sh
opencode run --standalone --format json "Use the structural-search skill. Find Python functions and classes in src."
```

The configured server name produces direct tool names such as
`treemcp_structural_search` and `treemcp_batch_search`. In the JSONL trace,
`tool_use` events show the skill ID, actual tool name, inputs and execution
outcome. If the server is unavailable, the skill reads its CLI guide and calls the
installed binary in the same workspace. This uses OpenCode's `read` and `shell`
tools and their permissions. A rejected path is reported to the user.
[CLI guide](../../integrations/opencode-v2/.opencode/skills/structural-search/references/cli-call.md)
[OpenCode MCP configuration](https://github.com/anomalyco/opencode/blob/v2.1.4/services/www/src/docs/content/mcp-servers.mdx)
and [skill discovery](https://github.com/anomalyco/opencode/blob/v2.1.4/services/www/src/docs/content/skills.mdx)

## Reproduce the checks

The [example project](../../tests/fixtures/opencode) contains the files used by
the skill examples. The Qt search captures `QObject` and `connect` on the
second line of `src/widget.cpp`. The Python batch finds functions `dispatch`
and `refresh` and class `Worker`, with two file reads and two tree builds.
CTest `search.queries` exercises the exact JSON examples over MCP stdio.

To see a partial batch, set `maxTotalMatches` to 1 in the Python example and
change the `classes` query to `(missing_node)`. The `functions` item returns
one match with `truncated: true`. The `classes` item reports an unknown node
at UTF-8 byte offset 1 and `incomplete: true`. The outer result carries both
flags. The empty match list for `classes` reflects the invalid query.

Run the [client scenarios](../../tests/check_opencode.py) from a source checkout
with Python 3, Git, OpenCode 2.1.4 and a running local OpenAI-compatible chat
endpoint. Pass the served model ID. On Windows, `--client` can point to the
native `opencode.exe` from the CLI package. The CLI fallback scenarios use
POSIX shell syntax, with Git Bash on Windows. The TreeMCP build must be able
to resolve its Qt and compiler runtime libraries.

```sh
python tests/check_opencode.py /absolute/path/to/structural-mcp \
  --client /absolute/path/to/opencode --base-url http://127.0.0.1:8080/v1 --model local-model
```

The runner installs the integration into a temporary Git project, configures the
local model there and uses private OpenCode data directories. It checks skill
loading, tool choices, captures, batch statistics, invalid queries, partial results,
workspace boundaries and CLI fallback with a disabled server. Model-generated queries and answers
can vary across runs.

## Original MCP verification record

The Windows x64 Release build of `structural-mcp 0.1.0` used Qt 6.4.1 and MinGW
11.2. Its executable SHA-256 was
`4b4844bfac60c62590c163c79e79d176e64ef5c83fa908697f42f66ee864b1f2`.
OpenCode 2.1.4 ran the project configuration with a local Gemma 4 26B A4B
QAT Q4 model served by llama.cpp b11748. A single query used
`treemcp_structural_search`. Two compatible queries used one
`treemcp_batch_search` and read two Python files once each. All six scenarios
passed on 2026-11-26, including the invalid item alongside a truncated valid
item, a rejected parent path and a disabled server. With the server disabled,
the client loaded the skill and reported that it could not verify the requested
matches.

Use the capture range to verify a reported location. During verification the
model sometimes quoted the zero-based `startRow` as an editor line number.
In the Python fixture, `refresh` has `startRow: 1` and appears on line 2.
