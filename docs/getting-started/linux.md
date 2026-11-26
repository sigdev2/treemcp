# Linux and WSL build

[Getting started](README.md)

Install a C++20 compiler, CMake 3.22 or newer, Ninja and Linux Qt Core development
files version 6.4.1 or newer. CMake discovers a system Qt installation directly.
For another Qt kit, set `CMAKE_PREFIX_PATH` to its root.

To keep local settings in a private file, copy the template once and edit its values:

```bash
mkdir -p .local
cp -n config/linux.sh.example .local/linux.sh
```

Load the settings and build from the repository root in your Linux or WSL shell:

```bash
source .local/linux.sh
bash scripts/build-linux.sh linux-debug
bash scripts/build-linux.sh linux-release
./build/linux-release/treemcp --self-check
```

The preset argument defaults to `linux-debug`. Each run configures, builds and
runs CTest. For ASan/UBSan, use `bash scripts/build-linux.sh linux-asan`.

Equivalent direct commands:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

See [configuration](configuration.md) for environment variables and custom presets,
and [deployment](deployment.md) for installation and archives.

## Launching from Windows through WSL

When tools and Qt are discoverable in the default WSL distribution:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-wsl.ps1 linux-release
```

Run from the repository root. The wrapper uses WSL's current-directory mapping and
invokes the Linux helper. It also accepts a custom preset from `CMakeUserPresets.json`.

For a custom Qt kit configured in `.local/linux.sh`, open a WSL shell, source that
file and run the Linux helper there. To select another distribution directly:

```powershell
wsl.exe --distribution <distribution> --exec bash scripts/build-linux.sh linux-release
```

Windows and Linux build directories remain separate. Windows Qt libraries cannot
produce a Linux executable. A checkout in the Linux filesystem is also supported.
