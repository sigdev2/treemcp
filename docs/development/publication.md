# Repository publication

Keep workstation paths in environment variables, `.local` or `CMakeUserPresets.json`.
These local files and generated build/install directories are ignored by Git.
Common secret containers, environment files and private keys are also ignored.
Public configuration templates contain placeholders and no account information.

Review the staged diff before committing or publishing. Git ignores do not remove
previously tracked files or history, and adding an ignore rule does not repair a
credential that was exposed.
Treat credentials outside these build settings as private data, never as examples.

Copyright holders and upstream license notices are intentional public attribution.
Preserve them; they are not machine configuration or access credentials.

Runtime archives are assembled from CMake's explicit install rules. Do not publish
an archive of the whole working directory, which may contain ignored local settings
or logs. CPack source packaging is explicitly blocked; use `git archive` on a reviewed
commit for source distribution. Release evidence should record toolchain versions without usernames,
machine names, private directory layouts or authentication data.

## Binary releases on GitHub

To publish a reviewed version, set the project version in `CMakeLists.txt` and
push a tag of the form `vX.Y.Z` at that commit. Pushing the tag starts automatic
publication. For example, tag `v0.1.0` requires project version `0.1.0`.

The Windows and Linux Release jobs package their tested x64 binaries with CPack,
extract each archive and run `treemcp --self-check`. Once formatting and all
five build configurations pass, the release job publishes both archives in
GitHub Releases with generated notes. The assets are named
`treemcp-X.Y.Z-win64.zip` and `treemcp-X.Y.Z-Linux.tar.gz`.
Only the release job requests `contents: write` for the built-in GitHub token.
Branch pushes, pull requests and manual CI runs perform the usual checks.
