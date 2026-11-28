# Search performance

The [benchmark driver](../../scripts/benchmark-search.py) creates a versioned
corpus and verifies its pinned SHA-256 digests before starting. The 1,000- and
10,000-file workloads share their generator with `batch.corpus`. The driver
checks the sparse matches against independent path, range and capture values.
It also compares every timed response with a fresh one-worker baseline on the
same filesystem. This checks traversal-order prefixes for structural search and
path-order prefixes for batch search independently.

## Reproduce the measurements

Build the reference and candidate in Release with the same compiler and Qt.
Run the driver from the candidate checkout. Keep each platform on the same
filesystem and hardware for both builds. Pass a JSON file with the source
revisions, build commands, toolchain and storage through `--metadata`.

```sh
python3 -B scripts/benchmark-search.py \
  --binary /path/to/reference/treemcp --binary /path/to/candidate/treemcp \
  --output build/search-performance.json --metadata build/search-environment.json \
  --workers 1 2 4 8 --warmups 2 --repeats 7 --resources --tail-repeats 40
```

On Windows use `python` and the paths to the two `treemcp.exe` files. Include
Qt and the compiler runtime on `PATH` when they live outside the binary
directory. Use `--case` to run a subset and `--tail-case` to collect a separate
p95 series for a specific workload. The default tail series measures the
short inline request sent during a batch and a cancellable scoped search.
If the server publishes the scoped reply before cancellation, the driver
drains replies through a ping, discards that attempt and retries. Each accepted
sample records `cancelRaceRetries`. Three consecutive races stop the run.
The ping is outside the measured interval.

The driver rotates configurations across repeats and excludes the warmup
requests from timing summaries. The OS file cache stays enabled throughout.
Responses are measured over persistent stdio sessions. CLI startup uses a
separate `--version` series. The JSON contains the raw samples, median, range,
wire bytes and files per second for workloads with a known completed file
count. With `--resources`, it also contains the process peak working set on
Windows or peak resident set on Linux and samples open handles or descriptors
about every 10 ms. Memory peaks cover a process's lifetime, including warmup.

## Profile a test build

Configure with `-DTREEMCP_SEARCH_PROFILE=ON` and pass `--profiles` a new directory.
This records JSON lines keyed by process and JSON-RPC ID. Production Release
builds omit the instrumentation. Compare instrumented timings within a separate
series because phase timers add work to each file and match.

The profile includes selected files, logical read calls, parse attempts, query
compiles and executions, queue waits, match collection and serialization. For
scoped structural search, `committed` counts files merged in discovery order.
Read or parse counts above it show speculative work at early termination.
Subtract `listVisitor` from `list` to measure directory enumeration exclusive
of callbacks. Scan time includes coordinator waits, and query time includes
collection. `serialize` sums individual compact JSON encodings during search,
including byte-budget checks and the MCP text content. For a published stdio
reply, add the `ns` of its `wireSerialize` event to include the final envelope
and LF. Match by PID and request ID. Wire bytes are recorded before queueing
the reply. `busy` includes file work on borrowed worker slots. Divide it by
workers times request wall time for the share of worker capacity used by the
request. `collectLock` reports lock acquisition count and total wait time.

In mixed workloads, join sample IDs and process IDs to `cancel` and `finished`
events. Their timestamp difference measures time from server cancellation to
completion of request cleanup. A `released` event covers cancellation before
the request started. In a profiled build, pass `--batch-buffer` once per
`--binary` to compare collection sizes from 1 to 64 on the same executable.
A size of one collects each match directly under the lock.

## Regression thresholds

On a dedicated runner, repeat the same Release configuration to establish the
normal spread before setting thresholds. Use comparisons of two binaries and
select targeted cases and worker counts. The optional `--max-median-ratio`,
`--max-short-p95-ratio` and `--max-memory-ratio` gates compare the candidate with
the reference and return a failing exit status on a breach. Enable the short
p95 gate with a mixed tail series and the memory gate with `--resources`.
Each run writes the raw samples before reporting a failed gate.

The control experiment on Linux/WSL2 added 20 ms before each parse in a build
of the reference checkout. With one worker on `large.single`, the median
increased from 221.3 to 385.5 ms. The driver rejected the 1.742 ratio with
`--max-median-ratio 1.20`. That threshold was used for this control experiment.
