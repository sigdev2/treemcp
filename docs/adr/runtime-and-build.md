# Runtime and build baseline

Status: accepted.

Use C++20, C11 for vendored C sources, Qt Core 6.4.1 or newer and CMake 3.22 or newer.
QCoreApplication owns the main event loop. No Qt GUI, Widgets, QML, Network or
Concurrent module is linked by the production target.

Windows presets use a Qt MinGW kit and its matching compiler. Linux uses a Linux
Qt installation and native GCC/Ninja, including when invoked through WSL.
Platform/configuration presets have distinct build directories. Tool locations
come from PATH, environment variables or ignored local configuration files.

CLI output stays on stderr. In server mode the Qt main thread dispatches
requests and writes protocol replies to stdout.

The style guide governs source conventions. Its general concurrency examples do
not override the project's requirement for controlled, bounded scheduling.
