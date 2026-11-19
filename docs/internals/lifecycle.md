# Request lifecycle

**Design requirement; search execution is not implemented yet.**

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

One request context owns a monotonic deadline, cancellation state and all budgets.
Check cancellation before admission, after dequeue, between directory batches,
around file reads, inside parsing/query progress callbacks and before publishing.
For an explicit MCP cancellation, stop all work and suppress the cancelled response.

EOF closes admission, cancels queued and running work, drains already completed
responses according to protocol rules and joins workers within the shutdown policy.
All Tree-sitter objects must be released before process exit.
