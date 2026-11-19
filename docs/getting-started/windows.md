# Windows build

[Getting started](README.md)

Install CMake 3.22 or newer and a Qt 6.4.1 or newer MinGW kit with its matching
MinGW compiler. Add CMake and MinGW's `bin` directories to PATH and set
`CMAKE_PREFIX_PATH` to the Qt kit root.

To keep these settings in a private file, copy and edit the template once:

```powershell
New-Item -ItemType Directory -Force .local | Out-Null
Copy-Item config/windows.ps1.example .local/windows.ps1
notepad .local/windows.ps1
```

Load your settings and build from the repository root:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
. ./.local/windows.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-windows.ps1 windows-debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-windows.ps1 windows-release
```

The preset argument defaults to `windows-debug`. Each run configures, builds and
runs CTest. Output goes to `build/windows-debug` or `build/windows-release`.
The execution policy option affects only the helper process.

Equivalent direct commands:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

See [configuration](configuration.md) for standard environment variables and custom
presets, and [deployment](deployment.md) for installation and archives.
