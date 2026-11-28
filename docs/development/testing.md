# Testing

## Continuous integration

The GitHub Actions workflow runs on pushes and pull requests and supports manual
dispatch. It builds the Windows/MinGW and Linux Debug and Release presets plus
`linux-asan`, `linux-tsan` and `linux-fuzz` with Qt 6.4.1. A separate job checks
`src/` and the native fuzz targets with clang-format 15.
Each configuration verifies every file in `third_party/` against `SHA256SUMS`,
including the dependency lock file. Update the manifest when vendored inputs change.

Before running CTest, CI checks the enabled suite names against
[`check-ci-inventory.py`](../../scripts/check-ci-inventory.py). Include new
mandatory suites in the inventory for their platforms. Both Release jobs also
run the MCP Python SDK 1.26.0 check and compare ignore fixtures with Git.
Each build job uploads a `ci-<preset>` artifact with the commit and toolchain,
configure/build logs, CTest inventory, JUnit results and test output as each
becomes available. Provisioning failures appear in the Actions job log.

The [release evidence matrix](release-evidence.md) maps each criterion to its
platform and result. Release jobs also save loaded or imported library names,
a trace verdict and the process-specific native logs. The Windows job verifies
the SHA-256 hashes of the NtTrace 1.1 executable and syscall definitions.

To reproduce the trace, run the matching command with a new output directory.
On Linux, a root test process uses `setpriv` to drop its mode-bit write override.
The fixture denies writes to both directories and its
source file before starting the server. The verifier follows server threads
from launch through EOF, counts actual fixture reads and matches protocol writes
to the frames the client received. On Linux it allows Qt eventfd notifications.
On Windows it checks file open rights, device controls and the output handle.

```sh
python3 -B tests/check_process_trace.py build/linux-release/treemcp build/linux-trace
python -B tests/check_process_trace.py build/windows-release/treemcp.exe build/windows-trace --nttrace C:/tools/NtTrace64.exe
```

The Windows tracer reads the matching `NtTrace.cfg` from beside its executable.
Use the [pinned release](https://github.com/rogerorr/NtTrace/releases/tag/v1.1).

## Malformed input and sustained concurrency

**Fuzzing.** The fuzz preset uses Linux, Clang 15 (including libFuzzer), Python
and the Qt kit on the CMake prefix path.

```sh
cmake --preset linux-fuzz
cmake --build --preset linux-fuzz
python3 -B scripts/fuzz-smoke.py build/linux-fuzz build/fuzz-smoke
```

The three in-process targets exercise the production code and its vendored C
dependencies under ASan/UBSan. The RPC target compares framing across chunk
boundaries, decodes records and round-trips serialized replies. The workspace
target checks selector and component validation, workspace glob compilation,
and matching against files and directories. The query target compiles C++ and
Python queries and runs predicates, capture copying and JSON construction on a
parsed source. It limits query bytes to 2 KiB, input to 8 KiB and copied
matches to 32. The workspace target accepts up to 4 KiB.

The driver seeds corpora from protocol cases, ignore rules and query fixtures.
CI mutates each corpus for 15 seconds per change and 900 seconds on the weekly
schedule. Each target has a 10-second input timeout and a 1,536 MiB RSS limit.
Keep the target log and failing input from the CI artifact. After a fix, add
the minimized input under `tests/fuzz/regressions/<rpc|workspace|query>/*.bin`.
The driver replays each saved input before mutation.

**ThreadSanitizer.** Use the `linux-tsan` configure, build and test presets.
The CI job builds the pinned Qt Base 6.4.1 Core target with Clang TSan so that
the detector observes Qt event delivery and semaphore synchronization. It uses
the ordinary Qt kit for build tools. This test build selects Qt's fork-based
process launcher because the LLVM 15 TSan runtime aborts in the test client's
`clone(CLONE_VM | CLONE_VFORK)` path. Configure treemcp with
`-DCMAKE_PREFIX_PATH=/path/to/qt-tsan-build -DQT_HOST_PATH=/path/to/qt-kit`.
The exact Qt configure command is in the [CI workflow](../../.github/workflows/ci.yml).

```sh
cmake --build --preset linux-tsan
setarch "$(uname -m)" -R ctest --preset linux-tsan
```

The test preset selects scheduler, stdio transport, publication, batch and
parallel search workloads. The process-local address layout allows the TSan
runtime to reserve its shadow mapping. CI saves per-process reports and exits
on the first report in each process.

**Release soak.** Run the same driver used by Linux Release CI on a machine
that can stay awake for 24 hours. Give each run a new JSONL filename and pass
the revision used to build the binary.

```sh
python3 -B scripts/soak-search.py build/linux-release/treemcp \
  --revision "$(git rev-parse HEAD)" --seed 7303 --output build/rc-soak.jsonl
```

Each round overlaps two batch submissions, cancellation of a single search,
a workspace edit and a scoped search for the new identifier. The client
correlates replies by ID and checks both batch captures. Four workers serve
the temporary workspace. The driver closes stdin and verifies exit before
opening a new process at the 16,384-request session limit.

The JSONL records the binary SHA-256, seed, completed rounds, overload and
cancellation counts, interval p95 and maximum round latency, current RSS,
kernel peak RSS and each shutdown. A round must finish within 45 seconds.
After a 60-second warm-up, peak RSS across all sessions must stay within
64 MiB of the warm-up high-water mark. CI shortens the duration to 30 seconds
and warm-up to five. A completed candidate record ends with `passed: true`
after at least 86,400 seconds.

## Local checks

For timing, resource measurements and optional regression gates, see
[search performance](search-performance.md).

Every build helper runs CTest after compilation. Current checks are:

- `bootstrap.runtime`: all nine grammar ABIs load; fixtures parse; queries return the
  expected capture boundaries; invalid node types are rejected.
- `bootstrap.broken-grammar` runs the CLI with the C++ grammar registered for
  Python. It checks the failing exit status, the Python diagnostic and clean stdout.
- `bootstrap.cli`: help, version and invalid options have the expected
  exit status and diagnostics; none writes to protocol stdout.
- `protocol.decoder` covers fragmented and oversized frames, strict JSON/UTF-8,
  ambiguous envelopes, integer precision and Unicode ID preservation.
- `protocol.session-memory` uses the Linux ASan allocator to check that a
  live session releases completed request payloads while remembering string IDs.
- `protocol.stdio` launches the real executable, negotiates the revision, validates
  the catalog and call results, pipelines 1,000 requests with mixed ID types,
  checks error recovery and closes stdin with replies still to read.
- `scheduler.workers` parks workers to check FIFO order and alternation when both
  queues have work. It checks that one batch can fill the pool and that further
  file submission expires while all slots are occupied. It also checks that
  cancelling a file-job owner wakes its blocked submitter and leaves the other
  owner's jobs available, and that a worker reserved during competing scans
  handles single requests.
- `scheduler.stdio` fills the request and cost budgets independently, checks
  overload recovery, expires queued and running calls, races cancellation with
  completion, and closes stdin with both coordinators and a sequential search
  active. It compares scoped search limits and diagnostic order at 1, 2, 4 and
  8 workers. Its shutdown grace interval is five seconds.
- Configure-time SHA-256 verification of the complete vendored source subset.
- The [OpenCode 2.1.4 scenarios](../integrations/opencode-v2.md#reproduce-the-checks)
  run separately from CTest and exercise the installed skill through a local model.
- `search.stdio` calls all three single-file tools over the wire. It covers C++
  and Python queries, UTF-8 offsets and columns, predicate diagnostics, syntax
  recovery, cursor exhaustion and the input, capture, tree and response limits.
- `search.queries` uses Python 3 to run the same queries against inline source,
  a file, a directory and a batch. Explicit expected matches cover Python
  imports, decorators, awaited calls and comprehensions, C++ includes, declarations,
  qualified templates and member calls, pairwise predicates over repeated
  captures, capture-free matches and searches on recovered syntax trees.
  It also exercises each new grammar, both interpretations of a shared C/C++ header
  and the exact Qt and Python arguments shipped in the OpenCode skill.
  The checks preserve capture order and verify UTF-8 ranges against the source.
- `search.parallel-order` gates reads on Linux so the second file finishes parsing
  and matching while the first is blocked. It compares the reply with one worker,
  injects a read error and checks descriptor cleanup after completion and cancellation.
  An ordered directory stream verifies early exit while the walker is filtering unrelated entries.
- `benchmark.mixed` exercises a late cancellation on either side of the other replies and
  checks the retry limit. In profiled test builds, `benchmark.serialization` checks the
  search encodings and the byte count of the final stdio reply.
- `workspace.stdio` runs when CMake finds Python 3. It uses the standard library
  and a temporary workspace to exercise filters, overlapping scopes, path and file
  limits, in-session edits, replacements, deletions and a directory swap during
  requests. It covers junctions and hidden attributes on Windows, and symlinks,
  FIFOs, sockets and a renamed workspace root on Linux.
- On Linux, `workspace.changed-read` interposes the first data read on each
  attempt in the test executable. A stable file takes one attempt, one mutation
  returns fresh bytes on the second attempt, and two mutations return a diagnostic.
- On Linux, `workspace.traversal-limits` supplies directory entries through
  `readdir`. It checks a real file at positions 100,000 and 100,001, a listing
  failure after a successful read, and type lookup when an entry has no type hint.

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

## Coverage audit, 2026-11-21

The baseline was commit `492ec28`. Each measurement used the full CTest suite
with Python available, a Debug build and fresh GCC counters. The figures cover
executable lines under `src/` compiled into `treemcp_core` and `treemcp`.
Branch counts include the outcomes emitted by gcov for those objects, excluding
arcs marked as throws. Platform-specific compilation changes the denominator.
Worker scheduling can change individual hit counts between runs.

CTest passed 14/14 in Windows Debug and Release, 17/17 in Linux Debug and
Release, and 18/18 with Linux ASan/UBSan. The Windows Release binary also
passed the MCP Python SDK 1.26.0 checks and the ignore fixture comparison
with Git 2.55.0.windows.5.

| Platform | Lines before | Lines after | Branch outcomes before | Branch outcomes after |
| --- | ---: | ---: | ---: | ---: |
| Windows, MinGW 11.2 | 1,904/2,052 (92.79%) | 1,939/2,052 (94.49%) | 2,829/3,465 (81.65%) | 2,925/3,465 (84.42%) |
| Linux/WSL, GCC 11.4 | 1,850/1,985 (93.20%) | 1,915/1,985 (96.47%) | 2,773/3,376 (82.14%) | 2,916/3,376 (86.37%) |

The review found a duplicate successful self-check invocation, an extension
check that accepted an empty list, and an ignore check that discarded duplicate
matches. The corpus comparison now also checks each sparse hit against its
known path, capture, range and flags. The Python suites fail immediately if
the interpreter disables assertions.

| Contract | Additional evidence |
| --- | --- |
| Protocol and transport | Exact depth, width and UTF-8 ID limits, mixed response envelopes, 16,384 accepted IDs followed by overload, and shutdown on a disconnected output pipe. |
| Grammars and single-file tools | CLI failure for a misregistered grammar, exact extension lists, diagnostic offsets, and preorder, ranges and missing-token flags for an incomplete declaration. |
| Workspace | Startup directory, extension selection, anchored globs, depth 64/65, bounded and Unicode-safe diagnostics, directory-entry limit, interrupted reads and I/O failure. |
| Batch | Exact global top-K membership, ties on the first capture, per-item isolation after parse or repeated-capture failures, and retention of completed matches at deadline publication. |

In an isolated Linux build, three deliberate changes admitted a 1,025-byte ID,
cleared the missing-token flag, and capped workspace diagnostics at 15. The
three corresponding baseline suites passed against that binary. The updated
decoder, search and workspace suites each failed on the affected behavior.
The 10,000-file corpus remains the most expensive fixture (12.13 seconds on the
instrumented Windows run). The Linux directory-entry boundary test took 0.73
seconds using a generated directory stream.

Residual paths include allocation and thread startup failures, resource-loading
errors, OS-specific handle failures and timing-dependent cancellation paths.
External client acceptance and long-running stress scenarios are tracked in
the [release criteria](acceptance.md).

To reproduce a measurement, use a fresh build directory, Python 3.9+ and the
gcov supplied with the compiler. On Windows select the `windows-debug` preset
and run the reporter with `python`. Use `--gcov` to select the matching binary.

```sh
cmake --preset linux-debug -B build/coverage '-DCMAKE_CXX_FLAGS=--coverage -fprofile-update=atomic'
cmake --build build/coverage
ctest --test-dir build/coverage --output-on-failure
python3 scripts/test-coverage.py build/coverage --missed
```

## Concurrency verification

On 2026-11-21, CTest passed 11/11 in both Windows/MinGW Debug and Release,
13/13 in both Linux/WSL Debug and Release, and 14/14 with Linux ASan/UBSan.
Both scheduler tests also passed 20 consecutive runs on Linux Release. The
Windows Release server passed the tool and schema checks with MCP Python SDK
1.26.0.

## Batch search verification

On 2026-11-20, CTest passed 8/8 on Windows/MinGW Debug and Release, 10/10 on
Linux/WSL Debug and Release, and 11/11 on Linux/WSL with ASan/UBSan. The Windows
Release server passed discovery, schema validation and tool calls for all five
tools with MCP Python SDK 1.26.0.

- `batch.correctness` checks schema budgets, item-local errors, filters, duplicate
  paths, deterministic top-K at 1, 2, 4 and 8 workers, byte limits on the wire,
  deadlines, cancellation, EOF, overload and a concurrent small search. Repeated
  requests observe edits and deletions. The Linux fixture also removes write
  permission on the workspace and the server's working directory.
- `batch.corpus` compares the full matches with independent single searches for
  16 queries over 1,000 files and two over 10,000 files. The corpus has a known
  distinct capture for every query.
- On Linux, `batch.shared-work` interposes native reads, Tree-sitter parses and
  tree deletion. It verifies that the same worker reads, parses and releases each
  fixture, and checks three reads and three parses for 16 queries over
  three files, fresh work on the next call, zero live trees on return, and zero
  additional reads or parses for invalid selectors and duplicate item IDs.

A Linux/WSL `strace -f -yy` run followed two 16-query batches in a read-only
workspace and working directory. The server wrote to protocol stdout and
anonymous event descriptors used for thread notifications. The trace showed
one program image, thread creation, and no writable file opens or sockets.

## Workspace verification

On 2026-11-19, all six CTest checks passed in Windows/MinGW Debug and Release.
All seven passed in Linux/WSL Debug, Release and ASan/UBSan. The Windows Release
server passed the Python SDK schema and tool-call checks for inline source,
a workspace file and a directory scope.

## Single-file search verification

On 2026-11-19, all five CTest checks passed on Windows/MinGW and Linux/WSL in
Debug and Release and on Linux with ASan/UBSan. The Windows Release server also
passed discovery, tool calls and input/output schema checks for all four tools
with MCP Python SDK 1.26.0.

## Protocol foundation verification

On 2026-11-19, build `45d7a50` (0.1.0, MCP `2025-11-25`) passed all four
CTest checks on Windows/MinGW and Linux/WSL in Debug and Release. Linux also
passed under ASan/UBSan. The Windows Release binary passed initialization,
ping, discovery, calls, JSON Schema validation and shutdown with the
[official MCP Python SDK 1.26.0](https://github.com/modelcontextprotocol/python-sdk/releases/tag/v1.26.0).

To reproduce the SDK check, use a development Python environment with
`mcp==1.26.0`, make the server's runtime libraries discoverable, then run
`python tests/check_sdk.py path/to/treemcp` from the checkout root.
The CTest suite uses Qt Core and runs offline.

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
