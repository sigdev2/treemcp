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

Exact commits are recorded in [dependencies.lock.json](../../third_party/dependencies.lock.json).
[SHA256SUMS](../../third_party/SHA256SUMS) covers every vendored source/header,
grammar metadata file and license. CMake validates these hashes on every configuration.
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
