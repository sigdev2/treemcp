# Request lifecycle

The main thread validates tool arguments and admits search requests. Protocol
messages, discovery and `languages` run there directly. The other four tools
share a fixed pool of 1 to 8 workers. The session has one coordinator for its
active batch and, with two or more workers, one for an active
`structural_search` with `paths`. The coordinators walk the workspace and wait
for their file jobs outside the pool. A scoped search uses the sequential path
when its coordinator is occupied or the pool has one worker. See
[batch execution](batch.md).

With W workers, the session admits at most `W + max(32, 4 * W)` outstanding
searches. Its cost budget is four times that limit. Each request costs one unit,
plus one per full 64 KiB of input frame, three for a `paths` scope and one per
batch query. Both limits include completed replies awaiting publication.
Exhausting either budget or submitting a second active batch returns JSON-RPC
error `-32001`. Cancellation releases a queued single's admission immediately.
Running requests keep their admission until cleanup.

The scheduler alternates FIFO queues for single requests and file jobs
whenever both have work. Either queue can use the full pool when the other is
empty. Each worker picks again after finishing a single request or a file.
Across both coordinators, at most W file jobs are queued or running, and
coordinators waiting for capacity are admitted in arrival order.
While both coordinators are active, the pool reserves one worker for single
requests as the current file jobs finish.

Scoped structural search merges file results in discovery order and keeps
Tree-sitter cursor order within a file. Scan diagnostics follow the same order.
Each pending file can retain up to 240 KiB of serialized match entries and the
number of pending files is bounded by W. Searches with a match limit of four
or fewer use a two-file window. The coordinator applies the request-wide match
and byte limits to the combined prefix and cancels its remaining file jobs once
it finds an additional match that does not fit. The walker checks the ready
prefix while filtering entries and before blocking for another file slot.
On POSIX, the coordinator can process a file under 1 KiB directly when it has no
earlier results to merge. It borrows one idle worker slot for that file and
yields to queued work. The size observed at open only informs scheduling.
Reading and checking the file still use the normal handle-based path.

Each search request validates its arguments and resource budget, determines the
current file set, securely opens and reads eligible files, builds fresh trees,
compiles queries, executes them and produces a bounded result. Sources, trees,
compiled queries, results and directory metadata are released with that request.

There is no application cache between requests: no ASTs, source buffers, query
objects, directory listings, outlines, symbol tables or pagination snapshots.
Only immutable configuration, schemas, extension mappings and compiled grammars
may remain process-wide. Worker parsers may be infrastructure objects, with a
reset after cancelled parsing and no retained user tree.

A coherent read applies to each file independently. Read through the opened
handle and check identity, size and modification metadata before and after reading.
Retry one changed read; after a second race, skip the file with a diagnostic.
This does not create an atomic repository snapshot.

The monotonic deadline starts when the main thread receives the complete frame.
It includes validation, queueing, execution and response preparation. A session
timer expires queued work even when every worker is busy. Running searches check
for interruption during traversal, reads, parser input, Tree-sitter progress,
predicate evaluation and result construction. Native file operations and the
bounded Tree-sitter query compilation call return before their caller can check
again. The main thread commits each reply to the output queue after a final
deadline and cancellation check. A dedicated writer drains replies in order.

The main thread serializes publication and cancellation for each JSON-RPC ID.
An explicit cancellation received before publication removes queued work, signals
running work and suppresses its reply. A notification arriving after publication
has no effect. Each coordinator removes and joins its own file jobs. Their
handles, source buffers and trees are released before the coordinator returns.

At EOF the session closes admission, cancels unfinished requests, joins workers
and publishes replies already completed at the shutdown boundary. The stdio
regression suite allows five seconds for cooperative shutdown on its local
filesystem fixtures. The server waits for native operations and worker cleanup
before exiting. Pending output, including the current record, is limited to
16 MiB. Exceeding that budget closes the transport with exit status 1. At EOF
the writer drains pending replies while stdout makes progress. One second
without a completed write during that drain interrupts the writer and reports
a transport failure.
