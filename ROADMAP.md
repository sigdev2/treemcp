# Roadmap and TODO

A checked item means implemented and locally verified, not full acceptance of the
minimum viable product (MVP).
Version 0.1.0 is the bootstrap; 1.0.0 requires all release gates below.

## 0. Bootstrap

- [x] Initialize CMake, C++20 and Qt Core with separate Windows and Linux presets.
- [x] Embed pinned Tree-sitter sources and the C++ and Python grammars.
- [x] Verify vendored file checksums at configure time; preserve dependency licenses.
- [x] Add an immutable language registry and an in-memory dependency self-check.
- [x] Organize English documentation and link the supplied formatting rules.
- [x] Verify Debug and Release builds and CTest on Windows/MinGW and Linux/WSL.
- [x] Verify an ASan/UBSan build on Linux.
- [x] Require strict compiler warnings as errors for project targets in all build configurations.
- [x] Keep machine settings private and provide portable build and deployment commands.

## 1. Protocol foundation

- [x] Verify and pin an official MCP revision; document its handshake and compatibility policy.[^protocol-policy]
- [x] Implement bounded UTF-8 NDJSON framing and strict JSON-RPC validation.
- [x] Add Windows and POSIX stdin transports, one main-thread stdout writer and EOF shutdown.
- [x] Implement initialize, notifications, tools/list, tools/call and request ID tracking.
- [x] Publish strict input/output schemas only for tools that actually work.
- [x] Test malformed input, interleaved requests, clean stdout and shutdown.

## 2. Single-file structural search

- [x] Implement languages using the compiled grammar registry.
- [x] Implement query_validate, syntax_tree and structural_search for inline single-file source.
- [x] Add request-local RAII parser/query/tree/cursor ownership and precise UTF-8 byte ranges.
- [x] Implement eq?, not-eq? and any-of?; reject unsupported predicates and directives.
- [x] Bound source, query, tree dump, match text, captures and complete response sizes.
- [x] Return canonical structuredContent plus compatible compact text content.

## 3. Workspace access and consistency

- [x] Bind one workspace root at process startup; accept only relative tool paths.
- [x] Implement POSIX handle-relative no-follow traversal and Windows final-handle checks.
- [x] Reject traversal, devices, FIFOs, reparse escapes and absolute paths before reading content.
- [x] Add streaming traversal, include/exclude globs, ignore rules and binary/hidden filtering.
- [x] Read through an opened handle, compare metadata before/after and retry at most once.
- [x] Verify file changes and removals are visible on the next request without restarting.

### Configurable search exclusions

- [x] Extend recursive traversal in `structural_search` and `batch_search` to skip
  directories with these exact names at any depth by default: `docs`, `cmake`,
  `build`, `CMakeFiles`, `dist`, `coverage` and `__pycache__`.
  Skip each directory before enumerating its contents.
- [x] Add a repeatable server argument `--ignore-dir <name>`. When supplied, its
  values replace the default directory list. Keep per-request `exclude` globs
  available for additional exclusions.
- [x] Test both searches with default and custom lists, including nested directories
  and similarly named siblings such as `build_tools`. Verify that dot-prefixed files
  and directories stay excluded with either list and that nested `.gitignore` rules
  still apply in visited directories.

## 4. Batch search (required for MVP)

- [x] Implement batch_search with unique IDs, explicit item languages and shared file scope.
- [x] Share each file read and each file/grammar parse only within one request.
- [x] Keep invalid grammar/query/predicate errors local to their items.
- [x] Apply total query bytes, file count, result, response and deadline budgets before admission.
- [x] Aggregate bounded deterministic top-K results; preserve input item order.
- [x] Distinguish truncated results, incomplete execution and true empty matches.
- [x] Pass the batch correctness, resource-limit and cancellation checks before declaring the API ready.[^batch-checks]

## 5. Concurrency, cancellation and deadlines

- [x] Implement a bounded scheduler with cost admission, overload errors and fair worker allocation.
- [x] Run one batch across available workers without unbounded child work queues or starvation.
- [x] Confine parsers, trees, cursors and source buffers to their owning workers.
- [x] Add monotonic deadlines and cooperative cancellation throughout traversal/parse/query/output.
- [x] Cancel all batch children and join them before cleanup; suppress cancelled responses.
- [x] Test shutdown grace periods, saturation, cancellation races and fairness.

## 6. Languages and integrations

- [ ] Add pinned C, Rust, JavaScript, TypeScript/TSX, Go and Java grammars after compatibility tests.
- [ ] Resolve ambiguous extensions such as .h through explicit language or static configuration.
- [ ] Pin and verify an OpenCode v2 release and its actual MCP/skill configuration format.
- [ ] Ship the structural-search skill, tested examples and installation instructions.
- [ ] Test skill discovery, single/batch tool selection, errors and partial results against a pinned client release.[^client-checks]

## 7. Quality, performance and delivery

- [ ] Add Windows and Linux CI, formatting checks and dependency integrity checks.
- [ ] Add protocol, security, no-persistence, determinism and concurrency acceptance suites.
- [ ] Add fuzz targets for JSON-RPC, queries, paths, globs, predicates and response construction.
- [ ] Add TSan coverage, ASan/UBSan coverage and a 24-hour concurrent soak run.
- [ ] Publish correctness-checked sequential/parallel/batch benchmarks with pinned corpora.
- [ ] Measure cancellation latency, read/parse amplification, peak memory and fairness.
- [ ] Establish performance gates on a dedicated runner; publish no unmeasured speed claims.
- [ ] Generate a release SBOM including Qt and package required runtime libraries and notices.
- [ ] Review the threat model, audit runtime dependencies and sign release artifacts.
- [ ] Pass all release acceptance checks for freshness, protocol, filesystem safety, batching and client integration before 1.0.0.[^release-checks]

## Open design decisions

- [ ] Resolve the open contract decisions in [requirements notes](docs/reference/requirements.md).
- [ ] Decide complete gitignore semantics and its implementation/dependency policy.
- [ ] Profile scheduler weights, batch admission costs and memory budgets.
- [ ] Select release sandbox profiles and large-file opt-in policy.
- [ ] Decide whether a future stateless pagination API is useful.

## After MVP

Consider outline, node_at, symbol_search templates, query_explain and file_metrics.
Semantic call graphs, renaming and source rewriting remain outside the initial scope.

## Notes

[^protocol-policy]: The [MCP compatibility policy](docs/adr/protocol-compatibility.md)
    defines what to record and verify before claiming support for a protocol revision:
    initialization, capabilities, message handling and compatibility tests.

[^batch-checks]: The [batch search acceptance checks](docs/development/acceptance.md#batch-search)
    cover result equivalence, one read/parse per file and grammar, overlapping paths,
    item errors, deterministic limits, partial results, freshness and fair scheduling.

[^client-checks]: The [OpenCode integration checks](docs/development/acceptance.md#opencode-integration)
    cover skill discovery, actual tool calls, correct single/batch selection and honest
    handling of errors or incomplete results in the documented client mode.

[^release-checks]: The [release acceptance criteria](docs/development/acceptance.md)
    describe each required suite and the evidence needed for 1.0.0. Bootstrap build
    checks alone do not satisfy these criteria.
