# Code style

Follow [StyleGuide.CPP.md](../../StyleGuide.CPP.md) for C++20 naming, ownership,
declarations and Qt conventions. Use the supplied [.clang-format](../../.clang-format)
with **clang-format 15**. Do not format vendored sources.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/format.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/format.ps1 -Check
```

The helper uses `clang-format` from PATH. Pass `-ClangFormat` to select another
executable, such as `clang-format-15`. Use version 15 with the supplied rules.

On Linux with clang-format 15 installed:

```bash
find src -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 |
  xargs -0 clang-format-15 --dry-run --Werror
```

The formatter's `Standard: c++17` setting controls formatting interpretation;
the actual compiler standard is C++20 in CMake and the style guide. It is retained
as supplied and can be revised when newer syntax requires a formatting policy.

Apply examples from the guide to the `treemcp` namespace and `TREEMCP_` header guards.
Project requirements constrain its general concurrency advice: Qt Core is the only
Qt runtime module, and the future scheduler must have bounded admission and explicit
fairness. Do not add QtConcurrent or use the QThreadPool queue as that scheduler.

Write documentation, comments, errors and diagnostic messages in English. Keep
architectural explanations in [internals](../internals/README.md), and use comments
for local invariants that cannot be expressed clearly by names and types.

## Compiler warnings

Every project C++ target uses `treemcp_set_warnings` from
[`cmake/Warnings.cmake`](../../cmake/Warnings.cmake). Strict warnings and
warnings-as-errors are mandatory in Debug, Release and sanitizer builds.

GCC and Clang use `-Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion
-Wsign-conversion -Wformat=2`, plus checks for casts, virtual functions, enum
coverage, implicit fallthrough, null dereferences, undefined preprocessor symbols,
missing declarations and floating-point promotion. GCC also checks duplicated
conditions and branches, logical operators, format bounds and signedness, and
unnecessary casts. The CMake file defines the complete compiler-specific list.
MSVC uses `/W4 /WX /permissive-`.

Fix warnings in project code instead of suppressing them or weakening the flags.
Apply the same policy when adding a target. Vendored Tree-sitter runtime and
generated grammar sources retain upstream compiler defaults and pinned checksums;
dependency headers are system includes. Qt is provided as a prebuilt dependency.
