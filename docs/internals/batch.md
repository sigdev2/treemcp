# Batch execution

**Design requirement; batch_search is not implemented yet.**

A batch is one `tools/call` containing multiple queries over one shared file scope.
It is not a JSON-RPC array or a set of independent requests. Each query has a unique
ID and an explicit grammar. Per-item file-scope overrides are outside the MVP.

Enumerate and filter files once, deduplicate overlapping paths, read each unique
file once and parse it once per required grammar. Queries for that grammar reuse
the tree within the current request. A changed-read retry is the only allowed
additional read. No source, tree or query is reused by a later request.

Distribute a bounded number of file jobs across workers. Each worker keeps its
parser/tree/cursor/source ownership and request-local compiled queries. A batch
uses one outer queue slot but consumes a cost and memory budget based on its
query count, query bytes and scan limits. It must share workers fairly with other requests.

Invalid query/grammar/predicate errors belong to the affected item. Invalid top-level
arguments, duplicate IDs, path violations and aggregate input limits reject the
whole request before workspace content is read. Cancellation affects every child job.

The completion barrier waits for all children before one response is serialized.
Items retain input order. With `sort=path`, use a stable UTF-8 path/byte-range/pattern/
capture ordering and bounded top-K selection; arrival order must not choose winners.
Distribute the global result budget deterministically in input-item order.

Report per-item and global `truncated` separately from `incomplete`. Empty matches
with a deadline, unsupported language or incomplete scope do not prove absence.
Count unique visited/read/parsed files separately from tree builds and query executions.

Initial budgets are 16 queries, 256 KiB aggregate query text, 200 total matches and
8 MiB serialized response, all within global caps. Exact configuration rules and
schema limits must remain synchronized; see [requirements](../reference/requirements.md).
