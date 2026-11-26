# Getting started

Build requirements: CMake 3.22 or newer, a C++20 compiler and Qt Core 6.4.1 or newer.
Qt 6.4.1 is the tested baseline on both operating systems. Tree-sitter
and all nine grammars are included in the repository; configuration and compilation
do not fetch dependencies. Python, Node.js and the Tree-sitter CLI are not required.

- [Windows with MinGW](windows.md)
- [Linux and WSL](linux.md)
- [Local configuration](configuration.md)
- [Installation and deployment](deployment.md)

The binary is named `structural-mcp`. Use `--help` for usage and
`--version` for the build version. The `--self-check` option parses an in-memory fixture for each language,
executes a query, checks capture boundaries and verifies invalid-query rejection.
It runs through a Qt Core event loop and never accesses project source files.

## One-shot tool calls

```text
structural-mcp [--workspace-root DIR] call TOOL [--args JSON|-]
```

Use any of the five tool names with the same argument object as in MCP.
The current directory is the default workspace root. Use `--args -` to
read one UTF-8 JSON object to EOF from stdin, or supply the object inline.
The CLI accepts up to 1 MiB of argument JSON. When `--args` is omitted,
the tool receives an empty object.

```powershell
structural-mcp call languages
'{"language":"cpp","path":"src/main.cpp","query":"(function_definition) @fn"}' |
    structural-mcp --workspace-root . call structural_search --args -
```

Exit 0 returns a single UTF-8 JSON line with the tool result. Check
`valid` for query validation and inspect diagnostics and completion
flags for partial search results. Exit 1 reports a tool or I/O failure
and exit 2 reports invalid invocation, input or workspace configuration.
Diagnostics go to stderr. [Tool arguments and limits](../reference/tools.md)

## MCP stdio

Start the executable without a subcommand as a stdio child process. Send each of
the following lines to stdin, waiting for the initialize response before
sending the notification. Read responses from stdout and close stdin to exit.

```jsonl
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"example","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"languages","arguments":{}}}
```

CLI diagnostics use stderr. Exit status is 0 for a successful self-check or
normal EOF, 1 for a failed self-check or transport error and 2 for invalid invocation.
