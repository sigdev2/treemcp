# Local build configuration

Run the helpers from the repository root in a configured shell. They use standard
CMake presets and environment variables:

| Setting | Meaning |
| --- | --- |
| `PATH` | CMake, compiler, build tool and optional clang-format executables |
| `CMAKE_PREFIX_PATH` | Qt kit root containing `lib/cmake/Qt6`; omit for system Qt |
| `CMAKE_BUILD_PARALLEL_LEVEL` | Number of parallel build jobs |
| `CC`, `CXX` | Optional compiler selection when configuring a new build tree |

Copy the [Windows](../../config/windows.ps1.example) or
[Linux](../../config/linux.sh.example) template into the ignored `.local` directory.
Replace the placeholders with your tool locations, then load the file in your shell.
These are ordinary shell scripts; the build helpers do not parse configuration files.

Windows:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
. ./.local/windows.ps1
```

The execution policy change lasts only for the current PowerShell process.

Linux or a WSL shell:

```bash
source .local/linux.sh
```

Keep Windows and Linux settings separate. Settings loaded in Windows do not
configure the Linux environment used by WSL.

For persistent CMake settings, use the ignored `CMakeUserPresets.json`. Define a
configure preset inheriting a public preset, with local `cacheVariables` or
`environment`, and matching build/test presets. Pass its name to the helper.
Build directories default to `build/<preset>`.

CMake caches toolchain and package locations. When switching compilers or Qt kits,
use a new build directory instead of reusing an incompatible cache.
