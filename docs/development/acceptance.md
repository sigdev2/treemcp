# Release acceptance criteria

The [current tests](testing.md) cover the build, MCP tools, workspace filters, handle checks, per-request freshness,
batch sharing, deterministic limits and batch cancellation.
Before version 1.0.0, implement the suites below and retain their fixture revisions,
toolchain versions, configuration and results with the release evidence.

## Freshness and persistence

- Run all tools in a read-only workspace and working directory. They must operate
  without creating caches, indexes, temporary files, state files or log files.
- Trace filesystem operations and distinguish process-owned writes from unrelated
  operating-system or loader activity. Diagnostics may use stderr only.
- Repeat a search after editing a file and then after deleting it. The next
  request must reflect the current files without restarting the server.
- Inspect request cleanup with instrumentation: no live source buffers, trees,
  compiled queries, directory listings or results may survive their request.
  Distinguish live data from allocator storage that has already been freed.
- Review the production code and linked dependencies for databases and other
  persistence mechanisms. Trace execution to verify no child processes or network
  access. Repeat these checks for both single searches and batches.
- Repeated-request performance must not depend on retained source or syntax data;
  record operating-system page cache effects separately.

## Protocol

- Verify the exact revision and handshake recorded in the
  [MCP compatibility policy](../adr/protocol-compatibility.md).
- Exercise at least 1,000 interleaved requests with distinct IDs. Every response
  must belong to its original request, regardless of completion order.
- Verify that stdout contains only valid protocol messages with the selected
  framing. Tool input/output schemas must match runtime validation and responses.
- Send malformed JSON, invalid arguments and unknown methods. The server must
  report appropriate errors without crashing or corrupting subsequent responses.
- Cancel queued and running requests, including batches. Work must stop promptly;
  cancelled requests must not publish further responses.
- Close stdin with work pending. Admission must close, queued/running work must
  stop, and worker cleanup must complete under the documented shutdown policy.

## Filesystem safety

- Reject absolute paths, parent traversal, NUL, device paths and platform-specific
  alternate paths that could escape the fixed workspace root.
- Attempt reads through symlinks or Windows reparse points leading outside the
  workspace, including replacement during traversal. No outside content may be read.
- Verify the opened handle identifies an allowed regular file. FIFOs, sockets,
  devices and directories must not be consumed as source files.
- Modify a file while it is being read. Retry at most once; a second detected
  change must skip the file with a diagnostic rather than report a coherent read.
- Check ignore, include/exclude, hidden and binary filtering before source parsing.
  Cover the root selector `.` and overlapping relative paths explicitly.

## Batch search

The batch API is ready only after all eight checks pass:

1. **Equivalent results:** compare 2-16 queries with independent single searches
   over the same scope and limits, including 1,000- and 10,000-file fixtures.
   Every item must contain the correct query's matches and original ID.
2. **Shared reads and parses:** instrument one read per unique file and one tree
   build per required file/grammar pair. The documented changed-read retry is the
   only exception. Multiple queries must not multiply file-read statistics.
3. **Overlapping scopes:** nested or repeated paths must not cause repeated reads
   or duplicate matches within an item.
4. **Error isolation:** an invalid grammar, query or predicate fails its item while
   valid items continue. Invalid batch structure, duplicate IDs, path violations
   and aggregate input limits must reject the request before workspace reads.
5. **Deterministic limits:** with `sort=path`, workers=1/2/4/8 and varied completion
   orders must select identical matches and item order for unchanged inputs.
   Global limits are assigned in input-item order; timing statistics are excluded
   from payload comparisons.
6. **Partial results and cancellation:** verify per-item and global result limits,
   complete serialized response size and deadlines. `truncated` and `incomplete`
   must explain partial output; explicit cancellation stops all children and
   suppresses the cancelled response.
7. **Fresh, read-only execution:** apply the freshness and persistence checks to
   repeated batches. Edits must be visible without restart or retained request data.
8. **Bounded and fair execution:** under large batches, cancellation and small
   requests must remain responsive. Bound child queues and memory, avoid worker
   starvation, and release child state before finishing the outer request.

Release readiness also requires the batch benchmarks described below; passing
these correctness checks does not establish a performance claim.

## OpenCode integration

- Pin the exact OpenCode release, server build, MCP revision and supported client
  mode. Keep the tested configuration and fixtures with the results.
- Validate skill frontmatter, discover the installed `structural-search` skill
  without ID collisions and load it through the client.
- Launch the server from the documented configuration, complete the handshake and
  discover all five tools: languages, query_validate, syntax_tree,
  structural_search and batch_search.
- Verify a single query uses structural_search and compatible queries over one
  scope use batch_search with correct item IDs and no redundant scans.
- Exercise invalid queries, item errors, truncated/incomplete results, ambiguous
  `.h` files, paths outside the root and unavailable tools. The workflow must report
  uncertainty or errors without inventing results or bypassing limits through shell access.
- Verify the actual tool namespace and call syntax in every advertised client mode.
  If only one mode is supported, document and test that mode explicitly.
- Validate C++/Qt and Python query examples against the pinned grammars. Keep
  deterministic schema/query checks separate from skill-guided agent scenarios.

## Performance and hardening

- Compare sequential single searches, concurrent single searches and one batch
  using identical queries, scopes, limits and a correctness oracle. Record pinned
  corpora, hardware, software, worker counts and process/OS cache conditions.
- Measure unique file reads, tree builds, query compiles/executions, serialized
  bytes, peak memory, cancellation latency and interactive latency during batches.
  Publish reproducible results rather than infer speed from fewer tool calls.
- Pass ASan/UBSan for parser/search paths and TSan for scheduling and transport.
  Exercise malformed input with fuzzing and resolve all known crashes.
- Run a 24-hour concurrent soak with cancellation, overload and changing files.
  Require no crashes, deadlocks or unbounded memory growth.
- Establish performance regression gates on a dedicated runner, audit production
  dependencies, review the threat model and generate a software bill of materials
  listing the shipped Qt, Tree-sitter and grammar revisions.
