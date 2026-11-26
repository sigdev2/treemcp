# TreeMCP command-line calls

Use the executable from the TreeMCP server configuration. The shipped
configuration uses `TREEMCP_EXECUTABLE`. If there is no configured executable,
use `treemcp` from PATH if available.

Use the same workspace root and directory exclusions as the MCP server.
OpenCode resolves the server's `cwd` relative to the active project directory
and uses that directory when `cwd` is omitted. Resolve a relative
`--workspace-root` against this server working directory. If the command
omits `--workspace-root`, use the server working directory as the root.
Pass that root to the CLI and copy each configured `--ignore-dir` option.
When there is no server configuration, use the active project directory and
the built-in exclusions. If a configured root or its exclusions cannot be
determined, stop and report which settings are unavailable.
Keep tool paths relative to the resolved root and keep the same tool
names and JSON arguments. Use `call languages` for a call without arguments.

The examples use the shipped configuration with the project directory as
the root and the built-in exclusions. For a custom configuration, substitute
the effective root and add the configured `--ignore-dir` options.
Pass complex arguments through stdin with `--args -`. Keep executable and
workspace paths quoted. Use the syntax of the client's shell.
OpenCode can run Bash on Windows.

## Single-file search in Bash or another POSIX shell

```sh
"${TREEMCP_EXECUTABLE:-treemcp}" --workspace-root . call structural_search --args '{"language":"python","path":"src/worker.py","query":"(function_definition name: (identifier) @name)"}'
```

## Single-file search in PowerShell

```powershell
$workspace = (Get-Location).Path
$treemcp = $env:TREEMCP_EXECUTABLE
if (-not $treemcp) { $treemcp = 'treemcp' }
$OutputEncoding = [System.Text.UTF8Encoding]::new($false)
@'
{"language":"python","path":"src/worker.py","query":"(function_definition name: (identifier) @name)","maxMatches":5}
'@ | & $treemcp --workspace-root $workspace call structural_search --args -
```

## Batch search in a POSIX shell

```sh
workspace="$PWD"
treemcp="${TREEMCP_EXECUTABLE:-treemcp}"
"$treemcp" --workspace-root "$workspace" call batch_search --args - <<'JSON'
{
  "paths": ["src"],
  "queries": [
    {"id":"functions","language":"python","query":"(function_definition name: (identifier) @name)"},
    {"id":"classes","language":"python","query":"(class_definition name: (identifier) @name)"}
  ],
  "maxTotalMatches": 10
}
JSON
```

Exit 0 returns one UTF-8 JSON object on stdout. Inspect capture text and
ranges, and check `truncated`, `incomplete`, `deadlineExceeded`,
`diagnostics` and per-item `error` where present before reporting coverage.
`query_validate` can return `valid: false` with exit 0.

Exit 1 reports a tool or I/O failure on stderr. Exit 2 reports invalid
invocation, JSON or workspace configuration. If the binary is missing,
lacks `call`, or the client cannot execute it, report that the structural
search could not run and end the search. Keep rejected paths outside
subsequent attempts.
