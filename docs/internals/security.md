# Security and resource limits

A stdio session owns an immutable workspace directory handle. Tool inputs select
relative names and the standalone root selector `.`. The directory walk streams
entries through open handles and applies scoped ignore and filter rules before
reading source. [Workspace inputs, filters and active limits](../reference/tools.md#workspace-files)

POSIX opens each child with `openat` and `O_NOFOLLOW`, checks its device, inode
and type against a no-follow metadata lookup, and uses nonblocking opens.
Windows opens a single component relative to the parent handle with
`NtCreateFile` and `FILE_OPEN_REPARSE_POINT`. It checks disk type, attributes
and the normalized final handle name. Child handles deny concurrent write and
delete sharing during inspection. On both platforms the reader compares the
opened file's identity, size, modification and change times before and after
the bounded read and retries a detected change at most once.

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

The active worker count is capped at eight. The [request lifecycle](lifecycle.md)
describes the enforced request and cost budgets.

This table describes target budgets for the full service. These are design budgets, not measured performance guarantees. Validate input
before enqueue and enforce resource limits during execution and serialization,
including both structured and text representations of the same response.

Threat tests must cover path races, symlink swaps, Windows reparse points, devices,
oversized input, pathological grammar/query execution, overload, starvation,
cancel/complete races, nondeterministic truncation and retention after requests.
