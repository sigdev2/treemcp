# Dependencies

Runtime components are C++ standard libraries, Qt Core, yyjson and embedded
Tree-sitter runtime/grammar sources. No external MCP SDK, Qt Network, Python, Node.js or shell
process is used by the executable. Development scripts are separate from runtime behavior.

| Component | Pinned source |
| --- | --- |
| Qt Core | Reference installation 6.4.1; minimum required 6.4.1 |
| yyjson (strict UTF-8 JSON reader) | [0.12.0](https://github.com/ibireme/yyjson/releases/tag/0.12.0) |
| Tree-sitter | [v0.25.10](https://github.com/tree-sitter/tree-sitter/releases/tag/v0.25.10) |
| C++ grammar | [v0.23.4](https://github.com/tree-sitter/tree-sitter-cpp/releases/tag/v0.23.4) |
| Python grammar | [v0.25.0](https://github.com/tree-sitter/tree-sitter-python/releases/tag/v0.25.0) |
| C grammar | [v0.24.2](https://github.com/tree-sitter/tree-sitter-c/releases/tag/v0.24.2) |
| Rust grammar | [v0.24.2](https://github.com/tree-sitter/tree-sitter-rust/releases/tag/v0.24.2) |
| JavaScript grammar | [v0.25.0](https://github.com/tree-sitter/tree-sitter-javascript/releases/tag/v0.25.0) |
| TypeScript and TSX grammars | [v0.23.2](https://github.com/tree-sitter/tree-sitter-typescript/releases/tag/v0.23.2) |
| Go grammar | [v0.25.0](https://github.com/tree-sitter/tree-sitter-go/releases/tag/v0.25.0) |
| Java grammar | [v0.23.5](https://github.com/tree-sitter/tree-sitter-java/releases/tag/v0.23.5) |

Exact commits are recorded in [dependencies.lock.json](../../third_party/dependencies.lock.json).
[SHA256SUMS](../../third_party/SHA256SUMS) covers the lock file and every vendored
source, header, grammar metadata file and license. On every configuration CMake
checks the hashes and rejects missing, unlisted or duplicate files.
The runtime's `lib/include` and `lib/src`, and each grammar's generated `src`, are
included directly. Grammar generators are not needed to build the repository.

Local Tree-sitter changes preserve parent constraints when matching wildcard
roots, retain every negated field assertion, recognize the full `MISSING`
keyword and release temporary capture data on unknown fields. The compiler
reports `TSQueryErrorCaptureLimit` at a capture that exceeds the three capture
slots on a query step.

To update a dependency, check out the intended upstream commit in a temporary
development directory, inspect its license and changes, copy the selected source
directories, re-evaluate the recorded local changes, and regenerate its revision
and hashes for the final checked-in bytes.
Review the diff and run both platform builds and grammar tests. Never replace an
exact revision with a moving branch or silently bypass a checksum failure.

The vendored components carry MIT licenses. Qt retains its own upstream licensing
terms; the project's MIT license does not replace them. Release bundles must carry
the applicable Qt/compiler notices and a generated SBOM. The lock file is provenance
metadata, not a complete release SBOM.
