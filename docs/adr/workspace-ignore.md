# Workspace ignore rules

Status: accepted design. The current implementation supports the subset documented
in the [tool reference](../reference/tools.md#filters-and-limits).

## Which paths to search

Apply ignore rules to every eligible workspace path, including tracked files and
workspaces without a Git repository. Discover rules from the workspace root down
to the candidate's parent. The only sources are the `.gitignore` and `.ignore`
files in those directories. The Git index, `.git/info/exclude`, user and system
Git configuration, and ancestors above the workspace root are outside this scope.
This gives two users searching the same tree the same ignore decisions.

At each level, append `.gitignore` rules in file order, then `.ignore` rules.
The last matching rule wins. For example, a child `.gitignore` can override a
parent `.ignore`, and a `.ignore` can override a sibling `.gitignore`.
Prune an excluded directory before reading its children or their ignore files.
Evaluate explicit selectors through the same ancestors. An ignored file yields
a tool error for single-file `path` and contributes zero candidates to `paths`.

Negation only changes the ignore decision. Hidden and special files, workspace
boundaries, `--ignore-dir`, request filters and language selection retain their
documented scope. Re-read rules on each request.

## Pattern contract

Use the Git 2.55.0 working-tree pattern syntax for both files. Match UTF-8 bytes
case-sensitively on both platforms, with `/` as the separator. Read LF or CRLF
and accept an initial UTF-8 BOM. Implement comments, negation, backslash quoting,
escaped trailing spaces, directory anchors, directory-only patterns, bracket
expressions and Git's component-aware `**` forms. Follow Git's per-pattern match
result for malformed wildcards, including a dangling escape that matches nothing.
Other rules in the file still apply. [Git pattern format](https://git-scm.com/docs/gitignore/2.55.0#_pattern_format)

| Rules in order | Expected search behavior |
| --- | --- |
| `out/`, `!out/keep.py` | The excluded parent prunes the whole directory. |
| `out/**`, `!out/keep.py` | The parent stays traversable and the direct child is searchable. |
| `a/**/z.py` | Both `a/z.py` and `a/b/z.py` are excluded. |
| `\#tag.py` | A file named `#tag.py` is excluded. |
| `n?.py` | `na.py` is excluded. `né.py` remains eligible because `é` occupies two UTF-8 bytes. |

Use the existing bounded workspace reader. Set limits of 64 KiB per ignore file,
512 UTF-8 bytes per pattern and 256 active rules across both files and all
ancestors. A missing file contributes zero rules. A failed read, an ineligible
ignore file, invalid UTF-8, binary data or an exceeded limit skips the affected
subtree with a diagnostic. Scoped searches continue through eligible siblings
and report `incomplete`.

## Implementation and verification

Implement a request-local matcher in project-owned C++ using existing Qt Core
and standard-library facilities. Compile ignore rules once per directory visit
and bound matching work with an iterative algorithm that checks cancellation.
Keep the existing request `include` and `exclude` glob syntax independent.
Workspace traversal already owns rule discovery, safe handles and deadlines.
A repository library would duplicate that work, and invoking Git would require
a subprocess and a Git installation on each server.

Use Git as a development reference. In an isolated temporary repository, compare
the excluded path set with `git check-ignore --no-index --stdin -z`, with
`core.ignoreCase=false` and empty external exclude sources. Record the Git
version with the expected results. Commit fixtures so ordinary builds and tests
can run offline without Git. [Git check-ignore](https://git-scm.com/docs/git-check-ignore)

Cover escapes, bracket classes, double stars, nested overrides, pruned parents,
UTF-8 names and malformed patterns in the reference fixtures. Test TreeMCP's
additional behavior through single-file and scoped structural search and batch
search on Windows and Linux, including mixed rule files, limits, explicit paths,
tracked files and changes between requests.
