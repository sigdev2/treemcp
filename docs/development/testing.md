# Testing

Every build helper runs CTest after compilation. Current checks are:

- `bootstrap.runtime`: both grammar ABIs load; fixtures parse; queries return the
  expected function boundaries; invalid node types are rejected.
- `bootstrap.cli`: help, version, self-check and failure cases have the expected
  exit status and diagnostics; none writes to protocol stdout.
- Configure-time SHA-256 verification of the complete vendored source subset.

The self-check uses explicit conditions, so validation remains active in Release.
It is a dependency integration check, not MCP, security or performance acceptance.

Strict compiler warnings and warnings-as-errors are always enabled for project
targets; see the [compiler warning policy](style.md#compiler-warnings).
Use the `linux-asan` preset for AddressSanitizer plus UndefinedBehaviorSanitizer. Instrumentation includes
the compiled Tree-sitter and grammar sources; the installed Qt library is not rebuilt.

Future tests are grouped by unit behavior, protocol, secure filesystem access,
determinism, batching, OpenCode integration and fixtures. The highest-value regressions
cover file changes between requests, no writes, no path escapes, cancellation,
bounded overload, one read/parse per batch and deterministic truncation.
The [release acceptance criteria](acceptance.md) define the required scenarios
and evidence for each suite.

Performance measurements require a correctness oracle, fixed corpus revisions and
a hardware/software manifest. Compare sequential searches, concurrent searches and
one batch using equivalent queries and limits. Report OS cache conditions separately
from process warmup; no application content cache is allowed.

## Bootstrap verification

Locally verified on 2026-11-18:

| Platform | Toolchain | Configurations | Result |
| --- | --- | --- | --- |
| Windows x64 | MinGW 11.2, Qt 6.4.1, CMake 3.24.2 | Debug, Release | Build and both CTest checks pass |
| WSL2 Ubuntu 22.04 x64 | GCC 11.4, Qt 6.4.1, CMake 3.22.1, Ninja 1.10.1 | Debug, Release | Build and both CTest checks pass |
| WSL2 Ubuntu 22.04 x64 | Same Linux toolchain | Debug with ASan/UBSan | Build and both CTest checks pass |

The supplied clang-format 15 rules also pass. This records bootstrap validation;
it does not establish future tool, client, security or performance compatibility.

## Portability and deployment verification

Verified on 2026-11-19:

- Mandatory strict warning flags are present in every project compilation command
  for Windows Debug/Release and Linux Debug/Release/ASan. All five configurations
  build without compiler warnings or errors and pass both CTest checks.
- The simplified helpers configure clean Debug builds and pass CTest on Windows
  and Linux/WSL with PATH and CMAKE_PREFIX_PATH loaded from private shell settings.
- Release builds and CTest pass on Windows and Linux/WSL using ignored local settings.
- Both platforms also build a clean source copy without local settings or previous
  CMake caches, in a checkout path containing spaces. Tools come from the environment
  on Windows and an explicit Qt argument on Linux.
- The Windows ZIP runs after extraction in another directory with only the operating
  system directory in PATH. The Linux tar.gz runs without LD_LIBRARY_PATH or Qt
  environment settings; Qt Core and ICU resolve inside the extracted archive.
- Archive text files and member names exclude local settings.

Package relocation checks establish compatibility with the tested host systems,
not every operating-system version or CPU architecture.
