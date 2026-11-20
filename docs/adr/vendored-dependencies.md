# Vendored and pinned dependencies

Status: accepted.

Commit the Tree-sitter runtime C sources, C++ and Python generated grammar sources
and their licenses. Record exact upstream commits in a lock file and verify every
vendored file with SHA-256 during configuration. Keep upstream bytes unchanged
except for reviewed local changes recorded in the lock file.

Builds must work without a dependency download or grammar generation step.
The executable cannot download dependencies or load arbitrary grammar plugins.
Dependency updates are explicit reviewed changes, followed by cross-platform builds,
query checks and relevant security/performance tests.

Start with two grammars to validate the build foundation. Expand the compiled set
only after each additional grammar has a pinned source, license and compatibility test.
