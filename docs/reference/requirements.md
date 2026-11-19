# Project requirements

| Area | Required behavior | Current status |
| --- | --- | --- |
| Language and Qt | C++20, Qt Core only | Implemented |
| Parser dependencies | Embedded, pinned Tree-sitter and compiled grammars | C++ and Python included |
| Search language | Tree-sitter S-expression queries | Single-file compilation, validation and execution |
| Protocol | stdio, UTF-8 NDJSON, JSON-RPC, one stdout writer | MCP 2025-11-25 on Windows and POSIX |
| Tools | languages, query_validate, syntax_tree, structural_search, batch_search | First four available, including workspace tree inspection and search |
| Freshness | Fresh file reads and trees per request | Implemented for single queries |
| Persistence | No workspace cache, state files or runtime logs on disk | No such runtime behavior |
| File access | Immutable root, secure handles, coherent reads | Implemented on Windows and POSIX |
| Scheduling | Bounded admission, fair workers and aggregate batch budgets | Planned |
| Cancellation | Cooperative cancellation, deadlines and child cleanup | Planned |
| Results | Bounded, precise ranges and deterministic sorted selection | Single-file ranges and limits implemented, workspace sorting planned |
| Client integration | Tested OpenCode v2 skill and configuration | Planned |

## Acceptance groups

The [release acceptance criteria](../development/acceptance.md) define the future
executable suites and the evidence required for each:

- [Freshness and persistence](../development/acceptance.md#freshness-and-persistence):
  fresh results after edits/deletions, no retained request data and no runtime writes.
- [Protocol](../development/acceptance.md#protocol): version conformance, interleaved
  requests, clean stdout, malformed input, cancellation and shutdown.
- [Filesystem safety](../development/acceptance.md#filesystem-safety): path boundaries,
  handle validation, replacement races, regular files and coherent reads.
- [Batch search](../development/acceptance.md#batch-search): equivalent results,
  shared reads/parses, item isolation, deterministic limits and fair scheduling.
- [OpenCode integration](../development/acceptance.md#opencode-integration): discovery,
  actual tool calls, correct tool selection and failure handling with a pinned client.
- [Performance and hardening](../development/acceptance.md#performance-and-hardening):
  correctness-checked benchmarks, sanitizers, fuzzing and sustained concurrent load.

## Open contract decisions

These must be settled before publishing stable schemas:

1. Define how an administrator may select batch sizes above the default 16 and
   below the hard cap 64. Advertised schemas must reflect the active limit.
2. Extend the single-file strict object definitions and partial-result reporting
   to the batch output contract.
3. Pin and test the [OpenCode configuration and skill format](../integrations/opencode-v2.md)
   before distributing a working client configuration.

Resolve these choices in architecture decisions and tests. Do not silently treat
an illustrative API example as a verified implementation contract.
