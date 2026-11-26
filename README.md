# TreeMCP

**Search code by its structure, from your AI coding assistant or terminal.**

TreeMCP uses Tree-sitter queries to find functions, classes, calls, and other
syntax patterns and return their exact locations in your project.

[Install](#install) · [Use with MCP](#use-with-mcp) · [Use from the CLI](#use-from-the-cli) · [Documentation](#documentation) · [License](#license)

## Why TreeMCP?

- **Match code structure.** Find function definitions, method calls, or
  imports even when their formatting varies.
- **Search your current workspace.** TreeMCP reads your files locally and
  searches them as they are, with no index to build or refresh.
- **Run related searches together.** Find several kinds of matches in one pass
  over the same files.

TreeMCP supports **C, C++, Rust, Python, JavaScript, TypeScript, TSX, Go, and Java**.

## Install

Build from source with CMake 3.22+, a C++20 compiler, and Qt Core 6.4.1+.
After installation, add the installed `bin` directory to your `PATH` to use the
CLI from any project.
[Windows build](docs/getting-started/windows.md) · [Linux and WSL build](docs/getting-started/linux.md) · [Installation](docs/getting-started/deployment.md)

## Use with MCP

1. Add a local stdio server in your MCP client. Use the absolute path to
   `treemcp` (`treemcp.exe` on Windows) and pass `--workspace-root`
   followed by the project directory's absolute path. [MCP setup](docs/getting-started/README.md#mcp-stdio)
2. Once connected, try asking your assistant, “Use TreeMCP to find Python function
   definitions in `src` and show their file locations.”

For OpenCode v2, TreeMCP includes a project configuration and a
`structural-search` skill. [OpenCode setup and examples](docs/integrations/opencode-v2.md)

## Use from the CLI

From your project directory, run `treemcp call languages` to list the
available languages. To search, run `treemcp call structural_search --args -`
and supply the query arguments as JSON on stdin. The CLI uses the same tool
arguments as MCP and prints the result as JSON for use in scripts.
[CLI commands and examples](docs/getting-started/README.md#one-shot-tool-calls)

## Documentation

- [Tool reference and query examples](docs/reference/tools.md)
- [Architecture and implementation](docs/internals/README.md)
- [Documentation index](docs/README.md) · [Roadmap](ROADMAP.md)

## License

TreeMCP is released under the [MIT License](LICENSE).
