# Getting started

Build requirements: CMake 3.22 or newer, a C++20 compiler and Qt Core 6.4.1 or newer.
Qt 6.4.1 is the tested baseline on both operating systems. Tree-sitter
and two grammars are included in the repository; configuration and compilation
do not fetch dependencies. Python, Node.js and the Tree-sitter CLI are not required.

- [Windows with MinGW](windows.md)
- [Linux and WSL](linux.md)
- [Local configuration](configuration.md)
- [Installation and deployment](deployment.md)

The binary is named `structural-mcp`. In this milestone it supports `--help`,
`--version` and `--self-check`. The self-check parses in-memory C++/Python fixtures,
executes a query, checks capture boundaries and verifies invalid-query rejection.
It runs through a Qt Core event loop and never accesses project source files.

Start the executable with no arguments as a stdio child process. Send each of
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
