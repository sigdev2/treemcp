---
name: structural-search
description: Find declarations, calls and other syntax patterns with TreeMCP when searching source files by their code structure.
---

Use the advertised TreeMCP tools when the MCP server is reachable. If those tools are absent or the client reports a connection failure, first read [references/cli-call.md](references/cli-call.md) using its path in the loaded skill's file list. Then follow that guide to run the equivalent `structural-mcp call` in the active workspace.

Discover the grammar IDs and suffixes with `languages`. For a shared `.h` suffix, select `c` or `cpp` from the project's language or the user's request. If the distinction matters and the language is unknown, ask or report both interpretations separately.

Use a shallow `syntax_tree` on a representative snippet or file when node names are uncertain. Check unfamiliar queries with `query_validate`. Its diagnostic offset counts UTF-8 bytes. After a query error, inspect the node or correct the query before retrying. Capture the smallest node that answers the question.

Use `structural_search` for one query, with `path` for one file or `paths` for a filtered scope. For several queries over the same files, send one `batch_search` with shared `paths`, an explicit language and a meaningful unique ID for each item. Use workspace-relative paths and a small match budget.

Report paths and captures as syntax evidence. Rows and UTF-8 byte columns start at zero. For example, a capture with `startRow: 1` is on editor line 2.

Check each batch item's `error`, `truncated`, `incomplete` and `hasSyntaxErrors`, plus the request diagnostics and deadline. A result limit can omit further hits. An incomplete scan can leave files unexamined. Qualify absence accordingly. Handle query errors, rejected paths and partial results in the current mode. Report a rejected path and keep the workspace boundary.

## Query examples

Find qualified `QObject::connect` calls in one C++ file with `structural_search`.

```json
{
  "language": "cpp",
  "path": "src/widget.cpp",
  "query": "((call_expression function: (qualified_identifier scope: (namespace_identifier) @owner name: (identifier) @method)) (#eq? @owner \"QObject\") (#eq? @method \"connect\"))",
  "maxMatches": 5
}
```

Find Python functions and classes across `src` with `batch_search`.

```json
{
  "paths": [
    "src"
  ],
  "queries": [
    {
      "id": "functions",
      "language": "python",
      "query": "(function_definition name: (identifier) @name)"
    },
    {
      "id": "classes",
      "language": "python",
      "query": "(class_definition name: (identifier) @name)"
    }
  ],
  "maxTotalMatches": 10
}
```
