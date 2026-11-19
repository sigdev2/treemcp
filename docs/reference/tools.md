# Planned MCP tools

**None of these tools is implemented or advertised yet.**

| Tool | Purpose | Key contract |
| --- | --- | --- |
| languages | List the compiled grammar registry | No workspace scan |
| query_validate | Compile and validate a query for a grammar | Exact error byte offsets; reject unsupported predicates |
| syntax_tree | Inspect syntax names in a snippet or file | Bound depth, nodes and output bytes |
| structural_search | Run one query in a file scope | Fresh files, precise captures, bounded results |
| batch_search | Run related queries in one shared scope | Unique IDs, one request context, shared reads/parses |

Search scopes use relative paths, include/exclude globs and ignore filtering.
Language selection is static extension mapping or an explicit language, never
content guessing. Ambiguous `.h` files need an explicit language or a configured default.

Positions use zero-based byte offsets, rows and byte columns. Search results carry
paths, pattern indexes, named captures, ranges, diagnostics and truncation information.
Batch results retain input-item IDs/order and distinguish item errors from request errors.

The canonical result is `structuredContent`; compatible text content remains compact.
The complete serialized response must stay within its budget. Tool registration
will include strict input and output JSON Schemas synchronized with runtime validation.

MVP predicates are intended to include `eq?`, `not-eq?` and `any-of?`.
Tree-sitter's C API exposes predicate steps; application code must implement them.
Do not silently accept `match?`, unknown predicates or arbitrary directives.

Matches are syntactic. They do not resolve symbols, overloads, types, semantic calls
or compiler-level relationships. MVP search does not rewrite source files and
does not offer pagination backed by stored snapshots.
