# Request lifecycle

The main thread validates tool arguments and admits search requests. Protocol
messages, discovery and `languages` run there directly. The other four tools
share a fixed pool of 1 to 8 workers. An independent coordinator walks the
workspace for the single admitted batch and waits for its file jobs. See
[batch execution](batch.md).

With W workers, the session admits at most `W + max(32, 4 * W)` outstanding
searches. Its cost budget is four times that limit. Each request costs one unit,
plus one per full 64 KiB of input frame, three for a `paths` scope and one per
batch query. Both limits include completed replies awaiting publication.
Exhausting either budget or submitting a second active batch returns JSON-RPC
error `-32001`. Cancellation releases a queued single's admission immediately.
Running requests keep their admission until cleanup.

The scheduler alternates FIFO queues for single requests and batch file jobs
whenever both have work. Either queue can use the full pool when the other is
empty. Each worker picks again after finishing a single request or one batch
file. The coordinator can submit at most W outstanding file jobs.

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
has no effect. Batch children release their handles, source buffers and trees
before the coordinator returns.

At EOF the session closes admission, cancels unfinished requests, joins workers
and publishes replies already completed at the shutdown boundary. The stdio
regression suite allows five seconds for cooperative shutdown on its local
filesystem fixtures. The server waits for native operations and worker cleanup
before exiting. Pending output, including the current record, is limited to
16 MiB. Exceeding that budget closes the transport with exit status 1. At EOF
the writer drains pending replies while stdout makes progress. One second
without a completed write during that drain interrupts the writer and reports
a transport failure.
