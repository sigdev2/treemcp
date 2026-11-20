# Batch execution

One `tools/call` runs an ordered list of queries over a shared workspace scope.
The dispatcher validates the schema, unique IDs, aggregate query bytes, selectors,
globs and budgets before admitting the request. One batch can run per session.
A second concurrent batch receives JSON-RPC error `-32001`. The main thread
continues to handle protocol messages and ordinary tool calls.

A coordinator compiles each query once, groups valid items by grammar and scans
the union of their file extensions. The workspace walker deduplicates overlapping
selectors. It reads each selected file through its opened handle, with one retry
for a detected change. The coordinator hands each coherent UTF-8 buffer to a
file worker. At most `--batch-workers` jobs are outstanding and the scanner
can hold one further file while awaiting capacity.

A worker owns its file's source, parser, trees and cursors. It builds one tree
per applicable grammar and runs that grammar's queries against it. Compiled
queries and predicate tables are read-only throughout execution. Workers copy
only the bounded capture data needed for results. No source, tree, compiled
query or result survives a completed request.

Workers insert matches into ordered per-item sets under one lock. They evict
the last match of an overflowing item, or the last match of the lowest-priority
nonempty item when the global count overflows. Across all items the sets retain
at most `maxTotalMatches` entries after each insertion. Sorting uses UTF-8 path
bytes, the first capture's start and end byte (zero for a captureless match),
pattern index and the sequence of displayed capture ranges and names. Equivalent
keys have identical displayed content within a coherent file read.

The coordinator joins the workers before building the response. It charges
the complete empty JSON-RPC envelope and adds matches in item and sort order.
Each entry consumes its structured JSON bytes, its escaped text representation
and any separating commas. The budget includes the terminating LF.

Cancellation and the monotonic deadline are checked during traversal, file
reads, parsing and matching. A deadline returns incomplete results. Explicit
cancellation and stdin closure stop the batch, join the workers and suppress
its reply. Only the main thread publishes responses.

See the [tool reference](../reference/tools.md#batch-search) for arguments,
limits, statistics and partial-result semantics.
