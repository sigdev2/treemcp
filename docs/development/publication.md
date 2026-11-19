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
