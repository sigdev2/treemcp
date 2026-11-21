# MCP tools

Use `tools/list` to discover the following tools. The linked descriptors contain
the input and output schemas embedded in the executable. Call a tool by passing
its `name` and `arguments` to `tools/call`.

| Tool | Required arguments | Result |
| --- | --- | --- |
| [languages](../../schemas/languages.json) | None | Compiled grammar IDs and their file extensions |
| [query_validate](../../schemas/query_validate.json) | `language`, `query` | `valid` and either a pattern count or a diagnostic with a UTF-8 byte offset |
| [syntax_tree](../../schemas/syntax_tree.json) | `language` and one of `source` or `path` | Nodes in preorder with depth, type, named/missing flags and range |
| [structural_search](../../schemas/structural_search.json) | `language`, `query` and one of `source`, `path` or `paths` | Matches in Tree-sitter cursor order with pattern indexes and ordered, named captures |
| [batch_search](../../schemas/batch_search.json) | `queries` and `paths` | One ordered result per ID, scan statistics and completion flags |

Supply `cpp` or `python`. Inline source and workspace files are parsed afresh on each call.

## Concurrent calls

`query_validate`, `syntax_tree`, `structural_search` and `batch_search` accept
`deadlineMs` from 1 to 300,000 milliseconds, with a default of 30,000. Time spent
waiting for a worker counts toward the deadline. An expired single call returns a
tool result with `isError` and the message `Request deadline exceeded`. A batch
returns its partial result with `deadlineExceeded` and `incomplete`.

Send `notifications/cancelled` with the original JSON-RPC ID in `params.requestId`
to cancel a queued or running search. When cancellation wins the race with
publication the server suppresses the reply. Overloaded calls receive JSON-RPC
error `-32001`. Ping and discovery remain available when admission is full.

## Workspace files

Start the server with `structural-mcp --workspace-root <directory>`. The default
root is the process working directory at startup. Use `path` to select one regular
file with the caller's explicit grammar, regardless of its extension. For example,
`{"language":"cpp","path":"src/main.cpp","maxDepth":2}` inspects a file with `syntax_tree`.

For a recursive structural search, select one or more files or directories in `paths`.
Use `.` for the root. Use the exact on-disk spelling and forward slashes. Empty,
dot and parent components, absolute paths, backslashes, device names and alternate
data streams are rejected. Paths must fit in 64 components and 4,096 UTF-8 input bytes.
Overlapping selectors visit a given relative file path once.

```json
{
  "name": "structural_search",
  "arguments": {
    "language": "python",
    "query": "(function_definition name: (identifier) @name)",
    "paths": ["."],
    "include": ["src/**/*.py", "tests/**/*.py"],
    "exclude": ["**/generated/"]
  }
}
```

Scoped searches select extensions advertised by `languages`. Each file-backed
match includes its relative `path`. Within each file, matches follow cursor order.
Across files, the server streams native directory order and applies one match and
response budget across the scope. It stops after detecting an omitted match.
Read, parse and traversal failures mark scoped results `incomplete`. The response
includes up to 16 diagnostics with relative paths clipped to 256 UTF-16 units.
A failed single-file read returns a tool error.

### Filters and limits

- With `paths`, both search tools skip directories named exactly `docs`, `cmake`,
  `build`, `CMakeFiles`, `dist`, `coverage` or `__pycache__` at any depth.
  Matching is case-sensitive. The server skips the directory before listing its
  contents or reading its `.gitignore`, even when a selector points inside it.
  To replace the defaults, pass one `--ignore-dir <name>` per directory name at
  startup, for example `structural-mcp --ignore-dir generated --ignore-dir vendor`.
  Single-file `path` requests can still open files inside these directories.
- `include` selects files. `exclude` can prune files or directories. Both take
  up to 64 case-sensitive globs of 512 UTF-8 bytes each and require `paths`.
  Up to 64 path selectors are accepted.
- A slashless glob matches a basename anywhere. A leading slash anchors it.
  Patterns containing a slash are relative to the workspace or the directory
  containing the ignore file. A trailing slash selects directories.
  `*`, `?` and bracket classes match within one component. A whole `**` spans
  components. For example, `src/**/*.py` also selects `src/main.py`.
- Each directory inherits its parents' `.gitignore` rules, then adds its own.
  Blank lines and lines starting with `#` are skipped. The last matching rule
  wins. A leading `!` re-includes an entry if its parent is still traversed.
  The supported subset excludes backslash escapes and external Git configuration.
  An unreadable or invalid ignore file skips its subtree with a diagnostic.
  Ignore files may contain up to 64 KiB and 256 rules may be active at once.
- Hidden names, Windows hidden attributes, links and reparse points, special
  files and binary data are excluded. UTF-8 text may contain tab, CR, LF and
  form feed. Other C0 control bytes are classified as binary. Explicit single-file
  requests also honor ignore rules and these exclusions.
- A workspace file may contain up to 8 MiB. A scan admits up to 10,000 candidate
  files and inspects up to 100,000 directory entries. Reaching a limit with more
  work pending marks the result incomplete. Read consistency is per opened file.
  An in-place change detected during a read allows one fresh attempt.

## Example

These `tools/call` parameters select the name of the Python function `answer`.

```json
{
  "name": "structural_search",
  "arguments": {
    "language": "python",
    "source": "def answer():\n    return 42\n",
    "query": "((function_definition name: (identifier) @name) (#eq? @name \"answer\"))"
  }
}
```

Pattern 0 returns one capture named `name` with text `answer`. Its byte range
is `[4, 10)`, on row 0 from byte column 4 to 10. All offsets, rows and columns
are zero-based and ends are exclusive. A tree or search result with
`hasSyntaxErrors: true` describes the parser's recovered tree.

## Predicates

- `(#eq? @capture "value")` requires every occurrence to equal the literal.
  `#not-eq?` requires every occurrence to differ.
- `(#eq? @left @right)` and `#not-eq?` compare occurrences pairwise in capture
  order. The two sequences must have equal lengths.
- `(#any-of? @capture "a" "b")` requires every occurrence to equal one of the
  listed strings. Supply at least one alternative.
- Predicates on the same pattern must all pass. Comparisons use complete source
  bytes before any display clipping. A missing optional capture has zero
  occurrences. A predicate can only reference captures declared by its pattern.
- Unsupported predicates (including `#match?`), directives and invalid arguments
  produce diagnostics. `query_validate` reports `valid: false` for an invalid
  pattern. Executing that query with `structural_search` returns a tool error.

## Single-file limits

| Resource | Limit |
| --- | --- |
| Inline source | 512 KiB UTF-8 |
| Query | 1 to 65,536 UTF-8 bytes, at least one pattern |
| Query compiler nesting | Combined delimiter depth and field prefixes in the enclosing top-level pattern up to 128 |
| Input record | 1 MiB including the JSON envelope and escaping, before LF |
| Tree depth (`maxDepth`) | Default 8, range 0–64, root at 0 |
| Tree nodes (`maxNodes`) | Default 256, range 1–2,048 |
| Matches (`maxMatches`) | Default 100, range 1–1,000 |
| Capture bindings on one query step | At most 3. Compilation reports the first binding that exceeds the limit. |
| Captures and text | First 64 captures per match, first 1,024 UTF-8 bytes per capture ending on a character boundary |
| Repeated captures | Up to 21,845 tree nodes for queries with a repeated capture, to keep Tree-sitter's 16-bit match capture count representable |
| Cursor | 256 simultaneous in-progress matches |
| Output | 240 KiB for serialized node or match entries, 1 MiB for the entire wire response including LF |

`structuredContent` is the canonical result. The text content contains the same
object serialized as compact JSON. `truncated` means a display limit omitted
nodes, matches, captures or text. `capturesTruncated` and `textTruncated` locate
clipping within a returned match. Ranges always describe the full capture.
`incomplete` means the query cursor dropped in-progress states or the workspace scope could only be searched in part. An empty match
array with both flags false means the query completed with no matches.

## Batch search

Use `batch_search` when several queries share file selectors and filters.
Each query requires a unique string `id`, an explicit `language` and a `query`.
The workspace traversal, extension filtering and predicates follow the same rules
as a scoped `structural_search`.

```json
{
  "name": "batch_search",
  "arguments": {
    "paths": ["src", "tests"],
    "queries": [
      {"id": "definitions", "language": "python", "query": "(function_definition name: (identifier) @name)", "maxMatches": 50},
      {"id": "cpp_calls", "language": "cpp", "query": "(call_expression function: (identifier) @callee)"}
    ],
    "maxTotalMatches": 100
  }
}
```

| Input | Default | Accepted range |
| --- | --- | --- |
| `queries` | Required | 1 to 16 items, up to 256 KiB aggregate UTF-8 query text |
| Item `id` | Required | 1 to 128 UTF-8 bytes |
| Item `maxMatches` | 100 | 1 to 200 |
| `maxTotalMatches` | 200 | 1 to 200 |
| `maxFiles` | 10,000 | 1 to 10,000 eligible candidates |
| `maxResponseBytes` | 8 MiB | 128 KiB to 8 MiB for the full NDJSON reply |
| `deadlineMs` | 30,000 | 1 to 300,000 milliseconds |
| `sort` | `path` | `path` |

The response's `items` array preserves input order and IDs. Earlier items
receive first claim on the global match and response budgets. Matches sort by
UTF-8 path, first capture range, pattern index and ordered captures. Sorting
covers the files actually searched. A file or deadline limit can leave part of
the scope unexamined.

An unknown grammar or invalid query produces an item-local `error` with a
message and, for query diagnostics, a UTF-8 byte offset. Each item reports
`truncated` for clipped or omitted output and `incomplete` for unfinished
execution or an error. A successful item with empty matches and both flags
false completed with no hits. The outer flags summarize all items.
`deadlineExceeded` identifies a deadline, and `diagnostics` records up to 16
workspace or execution diagnostics.

In `stats`, `filesVisited` counts eligible files admitted for a read and
`filesRead` counts coherent reads including binary files discarded by the
text filter. `filesParsed` counts distinct files yielding a tree. `treeBuilds`
counts successful file/grammar parses and `queryExecutions` counts item/file
searches. Counts apply to this request only.

Use `--workers 1` through `--workers 8` to set the shared search worker count.
The default uses the available CPU count capped at eight. The server also accepts
the existing spelling `--batch-workers`. One batch can run at a time per session.
