# Installation and deployment

Builds and packages are platform-specific. Build Windows binaries on Windows and
Linux binaries on Linux or WSL. An installed directory can be relocated on a compatible
system without preserving the build machine's Qt or compiler installation paths.

## Build, test and install

Run these commands from the repository root in a configured shell.

Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-windows.ps1 windows-release
cmake --install build/windows-release --prefix install/windows-release --strip
./install/windows-release/bin/treemcp.exe --self-check
```

Linux, including inside a WSL shell:

```bash
bash scripts/build-linux.sh linux-release
cmake --install build/linux-release --prefix install/linux-release --strip
./install/linux-release/bin/treemcp --self-check
```

Use `--prefix` to choose another installation directory. For a custom preset,
replace the build directory with its configured location.
Install into a fresh directory when upgrading a deployment so obsolete files from
an older install cannot remain in the bundle.

## Runtime libraries

By default, CMake copies the executable's shared runtime dependencies. Windows
places Qt Core and compiler DLLs beside the executable and excludes Windows system
DLLs. Linux places libraries under the install tree's library directory and uses
an executable-relative search path, including transitive dependencies. The Linux
loader and glibc remain operating-system dependencies.

No Qt installation or build PATH is required to run the copied directory. Native
ABI, CPU architecture and operating-system compatibility still apply: a Linux
binary built on a newer system is not automatically compatible with older glibc.
Use the oldest supported build environment for a distribution target.

Distribution packagers that supply dependencies through a package manager can set
`TREEMCP_BUNDLE_RUNTIME=OFF` in their CMake configuration. Sanitizer builds are for
development, not deployment.

## Archives

After a Release build, run CPack on the corresponding platform:

```powershell
cpack --config build/windows-release/CPackConfig.cmake
```

```bash
cpack --config build/linux-release/CPackConfig.cmake
```

CPack writes ZIP archives on Windows and tar.gz archives on Linux under
`build/<preset>/packages`. Archives contain only the explicit CMake install files:
the program, runtime libraries, documentation, configuration templates and notices.
The source checkout, `.local`, Git metadata and build logs are not packaged.

CPack source packaging (`package_source` or `CPackSourceConfig.cmake`) is disabled:
its directory walk does not follow Git ignore rules. After committing and reviewing
the intended source files, create a source archive from that commit instead:

```bash
git archive --format=tar.gz --output=build/treemcp-source.tar.gz HEAD
```

Before publishing a release, complete the [release acceptance criteria](../development/acceptance.md)
and the [dependency notice and SBOM work](../development/dependencies.md). Runtime
dependency copying is a technical deployment step, not a completed release audit.
