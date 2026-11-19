# TreeMCP

TreeMCP is a local code search server for AI coding assistants. It is designed to
find code by its structure, such as functions, classes and calls, and return precise
locations in the current project files. Several related searches can share one request.

The project is read-only by design: no source changes, persistent indexes or saved
search history. Structural matches describe syntax, not resolved symbols or program behavior.

**Status:** initial development. The build foundation and dependency self-check are
available; MCP tools and project search are planned.

- [Documentation](docs/README.md) and [build instructions](docs/getting-started/README.md)
- [Roadmap and TODO](ROADMAP.md)
- [C++ style guide](StyleGuide.CPP.md) and [clang-format rules](.clang-format)
- [MIT license](LICENSE)

Project documentation, messages and comments are written in English.
