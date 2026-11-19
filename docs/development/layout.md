# Source layout

| Path | Current responsibility |
| --- | --- |
| `CMakeLists.txt`, `CMakePresets.json` | Targets and platform build configurations |
| `cmake/` | Dependencies, integrity checks, warnings, sanitizers and version header |
| `src/main.cpp` | Qt Core application and CLI |
| `src/app/` | In-memory dependency self-check |
| `src/languages/` | Immutable compiled language metadata |
| `src/mcp/` | Bounded stdio transport, JSON-RPC decoder, session and tools |
| `schemas/` | Tool descriptors embedded in the executable |
| `tests/` | Runtime, CLI, decoder, stdio and client compatibility checks |
| `scripts/` | Windows, Linux, WSL and formatting helpers |
| `config/` | Shell environment templates with placeholder tool locations |
| `third_party/` | Vendored runtime/grammars, licenses, revision lock and file hashes |
| `docs/` | Hierarchical English documentation |
| `.local/`, `build/`, `install/` | Ignored machine settings and local artifacts |

Add `src/scheduler`, `src/search`, `src/filesystem` and `src/util` as
their implementations arrive. Future public headers belong in `include/treemcp`;
there is no public SDK yet.
Integration assets will live in `integrations/opencode-v2/`.

New directories should arrive with their implementations; the bootstrap does not
need empty source files or unimplemented classes.
