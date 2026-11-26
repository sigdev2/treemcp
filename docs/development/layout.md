# Source layout

| Path | Current responsibility |
| --- | --- |
| `CMakeLists.txt`, `CMakePresets.json` | Targets and platform build configurations |
| `cmake/` | Dependencies, integrity checks, warnings, sanitizers and version header |
| `src/main.cpp` | Qt Core application and CLI |
| `src/app/` | In-memory dependency self-check |
| `src/languages/` | Immutable compiled language metadata |
| `src/mcp/` | Bounded stdio transport, JSON-RPC decoder, session and tools |
| `src/scheduler/` | Shared worker pool and fair dispatch of searches and batch files |
| `src/search/` | Parsing, tree inspection, predicates and bounded single and batch search |
| `src/workspace/` | Handle-relative reads, directory traversal and filters |
| `schemas/` | Tool descriptors embedded in the executable |
| `integrations/opencode-v2/` | Project-local OpenCode configuration and structural-search skill |
| `tests/` | Runtime, CLI, decoder, stdio and client compatibility checks |
| `scripts/` | Windows, Linux, WSL and formatting helpers |
| `config/` | Shell environment templates with placeholder tool locations |
| `third_party/` | Vendored runtime/grammars, licenses, revision lock and file hashes |
| `docs/` | Hierarchical English documentation |
| `.local/`, `build/`, `install/` | Ignored machine settings and local artifacts |

Future public headers belong in `include/treemcp`;
there is no public SDK yet.

New directories should arrive with their implementations; the bootstrap does not
need empty source files or unimplemented classes.
