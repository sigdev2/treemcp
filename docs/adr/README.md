# Architecture decisions

Architecture decision records (ADRs) explain a design choice, its reasons and its
consequences. Each link below leads to a record or to the public design section
that currently defines the requirement.

Accepted means the design choice is adopted; it does not mean every associated
feature is implemented. Planned choices need implementation and validation; open
choices still require a compatibility or contract decision.

| Decision or design requirement | Status |
| --- | --- |
| [Tree-sitter query language](query-language.md) | Accepted |
| [Request-local state without persistent caches](request-state.md) | Accepted |
| [C++20, Qt Core and native platform builds](runtime-and-build.md) | Accepted |
| [Bounded scheduling and worker ownership](../internals/lifecycle.md) | Implemented |
| [Workspace boundaries and handle-based security](../internals/security.md) | Planned |
| [No subprocess, network or persistent runtime writes](../internals/security.md) | Adopted constraint |
| [Vendored dependencies and exact revisions](vendored-dependencies.md) | Accepted |
| [Workspace ignore semantics and matcher dependencies](workspace-ignore.md) | Accepted |
| [Search without stored pagination snapshots](../reference/tools.md) | Planned |
| [Deterministic ordering and bounded result selection](../internals/batch.md) | Implemented for batches |
| [Explicit predicate support](../reference/tools.md) | Implemented for single and batch search |
| [Coherent file reads and cancellation lifecycle](../internals/lifecycle.md) | Implemented |
| [MCP protocol compatibility policy](protocol-compatibility.md) | Accepted |
| [Request-local batch sharing and aggregation](../internals/batch.md) | Implemented for batches |
| [Batch admission, fairness and shared budgets](../internals/batch.md) | Implemented for batches |
| [Versioned OpenCode integration](../integrations/opencode-v2.md) | OpenCode 2.1.4 standalone direct tools |
