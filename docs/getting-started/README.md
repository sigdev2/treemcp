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

All CLI output goes to stderr; stdout is reserved for the future MCP transport.
Exit codes: 0 for success, 1 for a failed self-check and 2 for invalid invocation
or the unavailable server mode. Running without arguments deliberately exits
with a clear diagnostic. There is no MCP handshake or search tool yet.
