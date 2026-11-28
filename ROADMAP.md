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
- [x] Decide workspace ignore semantics and the matcher dependency policy.
  The [architecture decision](docs/adr/workspace-ignore.md) defines rule
  precedence, Git pattern compatibility, limits and reference tests.
- [x] Implement the full ignore-file pattern syntax in the project-owned matcher
  and verify it against the Git reference fixtures on Windows and Linux.

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

- [x] Add pinned C, Rust, JavaScript, TypeScript/TSX, Go and Java grammars after compatibility tests.
- [x] Select C or C++ for shared `.h` files through each query's explicit language.
- [x] Pin OpenCode 2.1.4 and verify its project-local MCP and skill configuration in standalone direct-tool mode.
- [x] Ship the structural-search skill, tested examples and [installation instructions](docs/integrations/opencode-v2.md).
- [x] Test skill discovery, single/batch tool selection, errors and partial results against a pinned client release.[^client-checks]

### One-shot `call` command and structural-search skill

- [x] Add `treemcp [--workspace-root DIR] call TOOL [--args JSON|-]`
  to `src/main.cpp`. Run one tool and exit. Use the current directory by default
  and honor the existing `--ignore-dir` and worker options. With no subcommand,
  start the stdio server as today. Show both modes in `--help`.
- [x] Accept one UTF-8 JSON object inline, or read it to EOF from stdin with
  `--args -`. Omitted arguments mean `{}`, so `treemcp call languages`
  returns immediately even with stdin open. Bound the argument input to 1 MiB
  while reading and diagnose malformed JSON, extra arguments and conflicting modes.
- [x] Dispatch through `Tools::call` and its validated runner using the existing
  workspace, scheduler, deadlines and `Tools::execute`. Cover all five tools,
  including the registry-backed `languages` and parallel `batch_search`.
  The CLI needs no server process or MCP handshake.
- [x] Write the tool's `structuredContent` object as one UTF-8 JSON line to
  stdout. Exit 0 for a returned result, including zero matches,
  `query_validate` with `valid: false`, and partial batches with item errors.
  Preserve their diagnostics and completion flags. Write failures to stderr
  with empty stdout. Use exit 2 for CLI syntax, malformed input, an unknown
  tool or an invalid workspace root, and exit 1 for a tool-reported failure.
- [x] Update the shipped [structural-search skill](integrations/opencode-v2/.opencode/skills/structural-search/SKILL.md).
  Use advertised MCP tools when available. When the client has no TreeMCP
  connection or cannot reach the server, read `references/cli-call.md` relative
  to the installed skill and run the same tool with the same workspace and
  arguments. A query error, rejected path or partial result is a tool outcome
  and should be handled in the current mode.
- [x] Add that reference alongside the skill. Document the configured
  `TREEMCP_EXECUTABLE`, quoting paths with spaces, selecting the project root,
  and passing JSON through stdin in PowerShell and POSIX shells. Include a
  single-file search, a batch search, exit codes and partial-result handling.
  If the executable is missing, lacks `call` or execution is unavailable,
  report the limitation. Link the guide from the OpenCode installation
  instructions and verify it ships in the installed skill directory.
- [x] Add Windows and Linux CLI tests comparing results with MCP
  `structuredContent` for all five tools on the same fixtures. Exercise inline
  and piped arguments, Unicode, a workspace path with spaces, ignore rules,
  rejected paths, malformed and oversized input, invalid queries and partial
  batches. Assert stdout, stderr and exit codes. Run the existing stdio suite.
- [x] Extend `tests/check_opencode.py` with a disabled-server scenario that
  reads the installed reference, invokes the configured binary and verifies a
  fixture capture. Scope the test's read and command permissions to that
  scenario. Also verify an unavailable binary produces a clear limitation and
  connected searches and rejected paths never trigger CLI fallback.

## 7. Quality, performance and delivery

Use the [current test inventory](docs/development/testing.md) as the baseline.
Start with 7.1. Acceptance, hardening and benchmark work can then proceed
in parallel. Use the benchmark results to set thresholds before enforcing them.
Package checks can start in CI as soon as both Release builds are available.

### 7.1 Reproduce the existing checks in CI

- [x] Add GitHub Actions jobs for the existing Windows/MinGW and Linux Debug
  and Release presets. Provision compatible Qt and compiler versions, Python
  and Git from a clean checkout. Run CTest and check the expected suite names
  on each platform so a missing Python interpreter cannot produce a green job.
- [x] Run clang-format 15 in check mode and keep CMake's vendored SHA-256
  verification enabled. Check manifest coverage as well as listed hashes so a
  new vendored input cannot enter a build without a checksum.
- [x] Run the Linux ASan/UBSan preset and the pinned MCP Python SDK check.
  Retain the commit, tool versions, preset, test inventory and failure logs.
  Use these runs as the per-change baseline.

### 7.2 Close gaps in release acceptance

- [x] Map each [release criterion](docs/development/acceptance.md) to a named
  test, platform and saved result. Reuse the 1,000-request protocol test,
  CLI/MCP parity, batch equivalence on 1,000 and 10,000 files, deterministic
  top-K at 1/2/4/8 workers, shared-work counters and scheduler tests.
- [x] Extend the read-only workspace and working-directory checks to all five
  tools. Trace process-owned file writes, child processes and network activity
  on Windows and Linux. Verify fresh results after edits and deletion in the
  same session and instrument request-owned objects through cleanup.
- [x] Fill uncovered platform-specific filesystem checks, including replacement
  during traversal, handle validation and the changed-read retry. Exercise
  cancellation before publication, batch child cleanup, overload and EOF under
  concurrent work. Record which assertions each platform actually exercised.

See the [release evidence matrix and verification record](docs/development/release-evidence.md).

### 7.3 Exercise malformed input and sustained concurrency

- [ ] Add opt-in bounded fuzz targets for JSON-RPC framing and decoding, query
  compilation, path validation, globs, predicates and response construction.
  Seed them from existing fixtures, run short smoke jobs per change and longer
  scheduled runs under ASan/UBSan. Keep minimized failures as regression tests.
- [ ] Add a separate Linux TSan preset for the scheduler, transport and batch
  workers. Run it on concurrent workloads and investigate each race report.
- [ ] Run a short CI smoke test of the soak driver, then run it for 24 hours
  against a release candidate. Mix searches, batches, cancellation, overload
  and workspace edits. Record the workload seed, progress, latency, peak and
  time-series memory, plus shutdown status. Require continued progress, clean
  shutdown and bounded memory after warm-up.

### 7.4 Measure performance and set regression thresholds

- See [search performance](docs/development/search-performance.md) for the
  Release comparison driver, profiling build and optional gates.
- [x] Pin the corpus generator or corpus revision, checksums, queries and
  expected matches. Compare sequential single searches, concurrent singles
  and one batch over the same scopes and limits in a persistent stdio session.
  Verify captures, ranges and completion flags against the oracle before
  accepting any timing sample.
- [x] Run Release builds at 1/2/4/8 workers. Record elapsed time, unique files
  and actual reads, required file/grammar pairs and actual tree builds, query
  compiles and executions, response bytes and peak process memory. Under a
  competing batch, measure small-search latency and time from cancellation to
  worker release. Keep test instrumentation outside the production protocol.
- [ ] Repeat measurements on a dedicated runner with recorded hardware,
  toolchain and OS page-cache conditions. Publish raw samples, median and tail
  latency, variability and reproduction commands. Derive regression thresholds
  from that baseline and verify the gate detects a controlled slowdown before
  making it a release requirement. Base speed claims on the published runs.

### 7.5 Verify and sign the release packages

- [ ] Build the Windows ZIP and Linux tar.gz through the existing CPack install
  rules. Extract each in a clean compatible environment and exercise
  `--self-check`, stdio discovery, a CLI search and the installed OpenCode skill
  files without a development Qt or compiler installation.
- [ ] Generate an SBOM for each archive from its actual contents and the
  vendored lock file. Account for Qt, compiler runtimes, transitive libraries,
  yyjson, Tree-sitter and all shipped grammars, including local changes. Match
  binary hashes and notices to the shipped components and document host-provided
  runtime dependencies.
- [ ] Review the [threat model](docs/internals/security.md) against the runtime
  dependency audit and acceptance results. Resolve release-blocking findings.
  Select a signing identity and verification procedure, sign the final
  artifacts or their checksum manifest, and verify both archives from a clean
  environment using the published verification instructions.

### 7.6 Qualify the 1.0.0 candidate

- [ ] Resolve the [stable batch-limit contract](docs/reference/requirements.md#open-contract-decisions)
  and check that advertised schemas match the enforced limits before tagging.
- [ ] Run the pinned OpenCode 2.1.4 scenarios with the packaged executable and
  skill, including MCP discovery, partial results, CLI fallback and an
  unavailable binary. Record the client, model, configuration and outcomes.
- [ ] Collect the acceptance matrix, sanitizer and fuzz results, 24-hour soak
  report, benchmark comparison, dependency review, SBOMs, notices, archive
  hashes and signature verification for the same candidate revision. Close
  stage 7 and tag 1.0.0 only when all required checks pass.[^release-checks]

## Open design decisions

- [ ] Resolve the open contract decisions in [requirements notes](docs/reference/requirements.md).
- [ ] Profile scheduler weights, batch admission costs and memory budgets.
- [ ] Select release sandbox profiles and large-file opt-in policy.
- [ ] Decide whether a future stateless pagination API is useful.

## 8. After MVP — file skeletons, call graphs and agent workflows

Start with Python and C++, the two languages covered by the existing OpenCode
fixtures. Deliver a usable skeleton first, then reuse its symbol locations to
root a call graph. Qualify each tool and language independently. codeTree's
file skeleton and caller/callee tools and mcp-server-tree-sitter's symbol and
usage queries provide reference workflows. [codeTree](https://github.com/ThinkyMiner/codeTree#readme)
and [mcp-server-tree-sitter feature matrix](https://github.com/wrale/mcp-server-tree-sitter/blob/main/FEATURES.md)

### 8.1 Extract a file skeleton

- [ ] Introduce `file_skeleton` with an explicit grammar and one workspace-relative
  file. Return declarations in source order with kind, name, enclosing declaration,
  compact signature and source range. Use the existing zero-based rows and UTF-8
  byte coordinates. Include enough information to select one overload or nested
  definition in a subsequent request.
- [ ] Extract signatures from grammar fields. Cover multiline signatures, return
  types, Python decorators and nested definitions, C++ namespaces, class members,
  templates, declarations and out-of-line definitions. Omit executable bodies and
  docstrings. Bound signature length, entry count and serialized response bytes.
  Surface clipping, unfinished extraction and syntax errors separately.
- [ ] Publish strict schemas and route MCP and `treemcp call file_skeleton` through
  the same runner. Compare both results to hand-authored outlines on Windows and
  Linux, including empty files, non-ASCII names and a unique sentinel inside a
  body that must never appear in the response. Measure the serialized result
  with the Gemma tokenizer on short files and files dominated by function bodies.

### 8.2 Trace callers and callees

- [ ] Introduce `call_graph` for a root definition selected by language, path and
  declaration location. Accept the location from a skeleton and check the expected
  qualified name against the current file. Return a diagnostic for a stale or
  ambiguous selection. Allow a known root to be queried directly. Start with
  direct callers or callees at depth one. Accept explicit workspace paths and
  existing traversal filters to bound cross-file lookup.
- [ ] Collect definitions and call sites within one request using the existing
  workspace reader. Reuse each file/grammar parse and release all workspace-derived
  data at completion or cancellation. Resolve supported direct calls using lexical
  scope and explicit qualification or imports. Represent ambiguous candidates and
  unresolved sites separately, with the spelling, location and reason. Cover
  overloads, shadowed names, indirect calls and dynamic dispatch. Define which
  edges can be followed in each grammar. [Request-local state](docs/adr/request-state.md)
- [ ] Return a directed node/edge list with caller, callee and call-site evidence.
  Add bounded multi-hop traversal for call trees, deduplicate nodes, preserve
  distinct call sites and stop expanding cycles. Choose deterministic ordering
  and documented depth, file, node, edge, response and deadline limits. Report
  scanned scope, truncation and incomplete scans so an empty incoming result can
  be interpreted within the examined files and supported resolution rules.
- [ ] Compare MCP and CLI with hand-labelled graphs for both directions, including
  a cross-file chain, unrelated functions with the same name, a diamond, recursion
  and a dynamic call. Assert exact sites and resolution status, fresh results
  after edits/deletion, deterministic limits, ignore rules and cancellation.
  Measure reads, parses, memory and latency for an incoming workspace search.
- [ ] Extend the support matrix to C, Rust, JavaScript, TypeScript, TSX, Go and
  Java as their fixtures pass. Expose per-language skeleton and graph support
  through discovery and return an actionable error for unsupported combinations.
  Exercise explicit C/C++ selection for shared headers.

### 8.3 Teach the installed skill to choose and combine tools

- [ ] Add the decision table below to the shipped skill as each capability becomes
  available. Put worked MCP and equivalent CLI calls in its references. Use the
  advertised capabilities and the configured workspace. Use the CLI fallback on
  connection loss or absent MCP tools as described in the existing guide. Handle
  rejected paths, query diagnostics and partial results in the active transport.
  [Structural-search skill](integrations/opencode-v2/.opencode/skills/structural-search/SKILL.md)
  and [CLI reference](integrations/opencode-v2/.opencode/skills/structural-search/references/cli-call.md)

| User's question | First action | When to take another step |
| --- | --- | --- |
| What API does this file expose? | Get its skeleton. | Stop once the signatures answer the question. For implementation details, inspect only the chosen declaration with a focused capture or a client read of its range. |
| Where does this syntax pattern occur? | Run one `structural_search` with a narrow capture. | If the grammar is uncertain, inspect a shallow `syntax_tree` and check the query with `query_validate`. |
| Where do several independent patterns occur in the same files? | Send one `batch_search` with shared paths and filters, an ID and grammar per query. | Inspect each item's diagnostics and completion flags. Split different scopes and retry only the items that need correction. |
| Who calls this method? What does it call? | Run `call_graph` in the requested direction at depth one. | If the root is ambiguous, select it from the skeleton first. Inspect a reported call site before attributing an ambiguous target. |
| How does execution reach another function? | Request a bounded multi-hop graph from the known endpoint, in the relevant direction. | Follow supported edges until the question is answered or the evidence runs out. Widen scope or depth only when a reported boundary affects the answer. |

- [ ] Include an end-to-end example starting with an unfamiliar class, selecting
  one method from its skeleton, then tracing its callers. Pass the returned
  location into the graph request. Keep dependent steps sequential.
  `batch_search` accepts Tree-sitter queries over a shared scope. Skeleton and
  graph calls use their own tool schemas. Show how to report an incomplete scan,
  unresolved dispatch and zero-based rows in the final answer.

### 8.4 Verify tool selection and token cost with Gemma

- [ ] Extend the existing OpenCode 2.1.4 harness with the instruction-tuned
  Gemma 4 26B A4B. Reuse the documented QAT Q4 / llama.cpp setup as a baseline.
  Record the exact model and tokenizer revisions, quantization, runtime build,
  chat template, decoding settings, context limit, skill and executable revision.
  [Existing client runs](docs/integrations/opencode-v2.md) and
  [Google's model card](https://huggingface.co/google/gemma-4-26B-A4B-it)
- [ ] Give the model task-only prompts for every row above, plus ambiguous roots,
  incomplete scans and disabled MCP. Run connected MCP and CLI fallback against
  the same fixtures. Score actual tool names, arguments, order, calls and final
  path/line evidence against acceptable routes and hand-labelled answers. Include
  a case solved with one skeleton, one requiring a batch and one requiring the
  skeleton-to-graph sequence. Reject invented edges and unqualified absence
  claims on partial results.
- [ ] Run paired trials on held-out prompts with the current and revised skills
  against the same tools and model. For file-overview tasks also compare with
  reading the full file. Record repeated-run success counts, unnecessary calls,
  full-file reads, latency and model-visible input/output tokens, including tool
  schemas, skill text and results. Publish per-task outcomes and variability.
  Accept the new workflow when fixture contracts pass, existing tasks retain
  correctness and correct overview answers on body-heavy files use fewer median
  input tokens than full reads. Set routing reliability targets from the baseline
  before evaluating the revised skill.

Other post-MVP candidates include node_at, symbol_search templates, query_explain,
file_metrics, compiler-backed call resolution, renaming and source rewriting.

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
