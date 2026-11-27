# Release acceptance evidence

The criteria appear in the same order as the [acceptance checklist](acceptance.md).
For each CI run, open the artifact `ci-<preset>`. A suite name in the tables
identifies its testcase in `ci/results.xml`. Its diagnostic output is also
in `<preset>/Testing/Temporary/LastTest.log`. Files under `build/ci/` in the
checkout appear under `ci/` in the artifact. The build job saves the Git revision,
toolchain and preset in `environment.txt` and the enabled suites in `inventory.json`.
“Both” means Windows/MinGW and Linux. Trace and SDK evidence use the two Release
presets. The allocation test runs in `linux-asan`.

## Freshness and persistence

| Criterion | Platform | Check and saved result |
| --- | --- | --- |
| All five tools in a read-only workspace and working directory | Both | `workspace.read-only` proves a real create in each directory and an append to the fixture are denied, checks all five replies and compares the final contents. Windows uses a deny ACL and Linux uses mode bits. |
| Process-owned filesystem writes and protocol-only stdout | Both | `check_process_trace.py` saves raw native calls in `build/ci/trace/` and the verdict in `trace.log`. Every protocol write must account for a received frame. `protocol.stdio` and `bootstrap.cli` check framing and diagnostics. |
| Edits and deletion without restarting | Both | `workspace.stdio` and `batch.correctness` compare captures before and after edits and require the deleted file to disappear in the same session. |
| Request data released | Linux ASan | `search.request-cleanup` records allocator events across the caller and worker during directory searches, including source, parse, query and result allocations. Each measured request uses changed input. Every observed allocation must be freed after the caller releases its reply. `protocol.session-memory` checks the live session's ID history separately. |
| Persistence dependencies, child processes and network access in singles and batches | Both | The production targets link Qt Core, yyjson and the embedded Tree-sitter runtime and grammars. Workspace opens request read access. The catalog reads compiled-in resources. Compare the [dependency inventory](dependencies.md) with `runtime-libraries.txt` and the Release trace of the running server. |
| Fresh work on repeated requests and OS cache effects | Both, Linux native counters | `workspace.read-only` and the process traces require repeated reads. `batch.shared-work` counts parses on the next call. Timed cold/warm page-cache measurements belong to 7.4 and have no result yet. |

## Protocol and transports

| Criterion | Platform | Check and saved result |
| --- | --- | --- |
| Pinned revision and handshake | Both | `protocol.stdio` and the 1.26.0 SDK run in `sdk.log`. |
| 1,000 interleaved request IDs | Both | `protocol.stdio` correlates the responses with their original requests. |
| Framing, schemas and transport parity | Both | `protocol.decoder`, `protocol.stdio`, `search.stdio` and `sdk.log` exercise decoding, discovery and validation. `search.cli` compares all five tools with MCP, including stdin JSON. |
| Recovery after malformed JSON, invalid arguments and unknown methods | Both | `protocol.decoder` and `protocol.stdio` assert error replies and subsequent successful requests. |
| Cancellation of queued, running and completed-but-unpublished work | Both | `scheduler.stdio`, `batch.correctness` and `scheduler.publication`. The latter delivers cancellation when the real single or batch completion event reaches the session, then verifies the following call publishes normally. |
| EOF under concurrent load | Both | `scheduler.stdio` closes input with single, queued and batch work pending and checks bounded exit and remaining stdout. |

## Filesystem safety

| Criterion | Platform | Check and saved result |
| --- | --- | --- |
| Absolute, parent, NUL, device and alternate paths | Both | `workspace.stdio` tests the same selector corpus through single-file and scope inputs. |
| Links and replacement during traversal | Both | `workspace.stdio` swaps a directory with a junction on Windows or a symlink on Linux during explicit and recursive searches. `workspace.entry-replacement` changes a listed regular file into a directory inside the enumeration callback. |
| Validate the opened handle | Both | `workspace.entry-replacement` checks the reopened entry's type. `workspace.stdio` covers Windows hidden/reparse files and write-sharing conflicts, and Linux sockets, FIFOs and a renamed root. Linux `workspace.traversal-limits` injects an inode mismatch at the final handle check. |
| One changed-read retry, then a diagnostic | Both | `workspace.changed-read`. Linux appends during the first native read and also injects EINTR and EIO. Windows denies a data writer and uses a separate attributes handle to change the real timestamp during reading. Both count attempts for zero, one and two changes. |
| Ignore/include/exclude, hidden, binary, root and overlapping paths | Both | `workspace.ignore-pattern`, `workspace.ignore`, `workspace.stdio`, `batch.correctness` and the Git reference run in `ignore.log`. Linux `workspace.traversal-limits` checks the listing boundary and fallback type lookup. |

## Batch search

| Criterion | Platform | Check and saved result |
| --- | --- | --- |
| 1. Equivalent matches and IDs | Both | `batch.corpus` compares 16 queries on 1,000 files and two on 10,000 against independent singles and known captures. |
| 2. Shared reads and parses | Linux, both | Linux `batch.shared-work` counts native reads and Tree-sitter parses for 16 items. Both run `batch.correctness` against expected statistics. |
| 3. Overlapping scopes | Both, Linux counters | `batch.correctness` checks matches and statistics. `batch.shared-work` counts actual operations on repeated selectors. |
| 4. Isolated item errors and request rejection | Both, Linux counters | `batch.correctness` checks malformed structures, aggregate limits and mixed valid/invalid items. `batch.shared-work` verifies zero additional reads/parses for an invalid selector and duplicate IDs. |
| 5. Deterministic global and item limits | Both | `batch.correctness` checks exact top-K matches and item order at 1/2/4/8 workers. |
| 6. Partial output, deadlines and cancellation | Both, Linux cleanup | `batch.correctness`, `scheduler.stdio` and `scheduler.publication` check limits, wire bytes, deadlines and suppressed replies. `batch.shared-work` cancels from a child releasing its tree and requires zero live trees at outer return. |
| 7. Fresh read-only batches | Both, Linux ASan | `batch.correctness`, `workspace.read-only`, `trace.log` and `search.request-cleanup`. |
| 8. Bounded and fair execution | Both | `scheduler.workers` checks queue capacity and ordering. It observes a queued child's state expire on cancellation and a running child's state expire before join returns. `scheduler.stdio` and `batch.correctness` check overload, recovery and interactive progress. Peak memory measurements belong to 7.4. |

## OpenCode integration

The [OpenCode verification record](../integrations/opencode-v2.md#original-mcp-verification-record)
identifies the Windows server binary and client used for the original scenarios.
The [scenario runner](../../tests/check_opencode.py) prints tool outcomes and the
answer. Retain its output and the packaged binary hash for the 7.6 candidate run.

| Criterion | Platform | Check and saved result |
| --- | --- | --- |
| Exact versions, fixture and mode | Windows | OpenCode verification record, project config and `check_opencode.py` version check. |
| Discover and load the intended skill | Windows | `check_opencode.py:scenario` checks the installed ID and actual skill body returned by the client. The fixture contains a single project skill. See the verification record. |
| Handshake and discovery of five tools | Windows client, both servers | `check_opencode.py:exercise` invokes all five configured tools. `protocol.stdio` and `sdk.log` assert the catalog and handshake directly. |
| Route one query and compatible batches | Windows | `check_opencode.py:exercise` checks single/batch choice, distinct item IDs and read/parse counts. See the verification record. |
| Errors, partial output, shared headers, path rejection and unavailable tools | Windows client, both servers | Client scenarios check partial output, explicit C, rejected paths and unavailable MCP. `search.queries` checks both C/C++ interpretations. The current CLI fallback scenarios require a fresh candidate record under 7.6. |
| Namespace and call syntax | Windows | OpenCode standalone direct-tool scenarios assert the actual `treemcp_*` calls. See the verification record. |
| Shipped Qt and Python examples | Both | `search.queries` checks the exact examples against pinned grammars independently of model output. |

## Performance and hardening

| Criterion | Platform | Check and saved result |
| --- | --- | --- |
| Sequential singles, concurrent singles and one batch with a correctness oracle | Both Release | `batch.corpus` provides the correctness baseline. The 7.4 benchmark and pinned environment report are pending. |
| Operation counts, bytes, peak memory and latency | Both Release | `batch.correctness` and Linux `batch.shared-work` cover functional counts and byte limits. The 7.4 measurement series is pending. |
| Sanitizers and malformed input | Linux | `ci-linux-asan` saves ASan/UBSan CTest results. The TSan preset and fuzz runs are pending in 7.3. |
| 24-hour concurrent soak | Candidate runner | The seeded soak and time-series report are pending in 7.3. |
| Dedicated-runner gates, dependency audit, threat review and SBOM | Both packages and dedicated runner | Release traces and library inventories cover the exercised binary. Thresholds are pending in 7.4. The package audit, threat review and SBOM are pending in 7.5. |

## Local verification, 2026-11-27

The five local presets used Qt 6.4.1, MinGW GCC 11.2 on Windows and GCC 11.4
on Ubuntu 22.04 under WSL2. Windows Debug and Release each passed 19/19 tests.
Linux Debug and Release each passed 21/21. Linux ASan/UBSan passed 23/23.
Both Release binaries passed the MCP Python SDK 1.26.0 and Git ignore fixture checks.
Both Release traces recorded one server and five source reads across one tree
inspection, two single searches and two batches. Eight protocol writes matched
the eight replies. Neither tracer observed a filesystem mutation, child process
or network operation. The ASan fixture found zero live measured allocations
after releasing each search reply.
