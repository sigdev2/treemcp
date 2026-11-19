# OpenCode v2 integration

**Planned deliverable. OpenCode v2 integration awaits client-specific testing.**

Ship a `structural-search` skill under
`integrations/opencode-v2/.opencode/skills/structural-search/SKILL.md`, with query
references, fixture-backed examples and a tested native client configuration.
Do not distribute an example as working until a specific OpenCode v2 release and
MCP revision have passed an end-to-end connection and tool-discovery test.

The skill will guide users through grammar discovery, optional tree inspection,
query validation and narrow search scopes. One query uses structural_search;
multiple queries with compatible scopes use batch_search with meaningful unique IDs.
It must explain diagnostics, truncation and incomplete results, and discover the
actual tool namespace from the client rather than assume a particular spelling.

Examples must cover C++/Qt and Python, single and multiple files, batches, invalid
queries and partial output. Verify node names against the pinned grammars.
Keep context small with selective captures, low result limits and narrow scopes.

The skill must respect workspace boundaries and never use shell or network access
to bypass rejected MCP operations. Do not claim semantic symbol resolution or infer
absence from incomplete results. Host connections must retain workspace isolation.

Release checks include valid frontmatter, skill discovery, actual MCP tool calls,
single/batch selection, item errors, unavailable tools and the documented client mode.
