# Security and resource limits

**Target security model; the bootstrap does not expose filesystem search tools.**

Bind the workspace root once at startup. Client paths are workspace-relative.
Reject absolute paths, parent traversal, NUL, device/alternate paths and escapes.
The root selector `.` needs an explicit normalization rule. Hidden files, `.git`,
binary files and symlinks are excluded by default; ignore rules apply before reads.

String canonicalization followed by opening a path is not a sufficient security
boundary. POSIX requires directory-relative handle walking with no-follow semantics.
Windows requires validation of the final opened handle and reparse behavior.
Only regular files are eligible. Identity and consistency checks use the opened handle.

The production executable must not invoke shells, git, compilers or other tools;
open outbound sockets; or write caches, indexes, logs, temporary files or state.
Diagnostics go to stderr. Build helpers may invoke build tools and write artifacts;
these are development operations, separate from server runtime behavior.

| Resource | Initial default | Hard cap |
| --- | --- | --- |
| Workers | min(hardware threads, 8) | 64 |
| Queued requests | max(32, 4 x workers) | 4096 |
| Source file | 8 MiB | 256 MiB |
| Files per request | 10,000 | 100,000 |
| Single-search results | 100 | 10,000 |
| Deadline | 30 seconds | 300 seconds |
| Context lines | 0 | 20 |
| Overall response | 16 MiB | 64 MiB |
| Batch query count | 16 | 64 |
| Batch query text | 256 KiB | 1 MiB |
| Batch results | 200 | 10,000 |
| Batch response | 8 MiB | 64 MiB |

These are design budgets, not measured performance guarantees. Validate input
before enqueue and enforce resource limits during execution and serialization,
including both structured and text representations of the same response.

Threat tests must cover path races, symlink swaps, Windows reparse points, devices,
oversized input, pathological grammar/query execution, overload, starvation,
cancel/complete races, nondeterministic truncation and retention after requests.
