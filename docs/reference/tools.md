# MCP tools

Use `tools/list` to discover the following tools. The linked descriptors contain
the input and output schemas embedded in the executable. Call a tool by passing
its `name` and `arguments` to `tools/call`.

| Tool | Required arguments | Result |
| --- | --- | --- |
| [languages](../../schemas/languages.json) | None | Compiled grammar IDs and their file extensions |
| [query_validate](../../schemas/query_validate.json) | `language`, `query` | `valid` and either a pattern count or a diagnostic with a UTF-8 byte offset |
| [syntax_tree](../../schemas/syntax_tree.json) | `language`, `source` | Nodes in preorder with depth, type, named/missing flags and range |
| [structural_search](../../schemas/structural_search.json) | `language`, `source`, `query` | Matches in Tree-sitter cursor order with pattern indexes and ordered, named captures |

Supply `cpp` or `python` and the full text of one source file or snippet.
The server parses the supplied text on each call. Reading workspace paths and
running `batch_search` belong to later milestones.

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
| Captures and text | First 64 captures per match, first 1,024 UTF-8 bytes per capture ending on a character boundary |
| Repeated captures | Up to 21,845 tree nodes for queries with a repeated capture, to keep Tree-sitter's 16-bit match capture count representable |
| Cursor | 256 simultaneous in-progress matches |
| Output | 240 KiB for serialized node or match entries, 1 MiB for the entire wire response including LF |

`structuredContent` is the canonical result. The text content contains the same
object serialized as compact JSON. `truncated` means a display limit omitted
nodes, matches, captures or text. `capturesTruncated` and `textTruncated` locate
clipping within a returned match. Ranges always describe the full capture.
`incomplete` means the query cursor dropped in-progress states. An empty match
array with both flags false means the query completed with no matches.
