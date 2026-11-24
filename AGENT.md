# Directory Diff development guide

Directory Diff is a C++20 and Qt 6 utility inspired by Araxis Merge's folder comparison. Its goal is a small, dependable tool for comparing two directory trees, inspecting text changes, and applying deliberate copy or delete operations.

## Code map

| Path | Responsibility |
| --- | --- |
| [`CMakeLists.txt`](CMakeLists.txt) | Builds `directory_diff_lib`, `directory_diff_cli`, and `directory_diff_gui`. The `QT6_DIR` environment variable supplies the Qt kit path. |
| [`include/directory-diff/`](include/directory-diff/) | Public comparison types and functions in `dirdiff.h` and `filediff.h`. Third-party headers live under `3rdparty/`. |
| [`src/lib/dirdiff.cpp`](src/lib/dirdiff.cpp) | Recursive file and folder scan, relative-path matching, ignore patterns, and result callbacks. |
| [`src/lib/filediff.cpp`](src/lib/filediff.cpp) | File size and xxHash comparison, line loading, Myers diff, and unified diff output. |
| [`src/cli/main.cpp`](src/cli/main.cpp) | CLI11 commands `dir-diff` and `file-diff`. |
| [`src/gui/qmainwindow.cpp`](src/gui/qmainwindow.cpp) | Main toolbar, tabs, directory filters, and opening a file diff from a directory result. |
| [`src/gui/qdirdiff.cpp`](src/gui/qdirdiff.cpp) | Shared directory tree model and filter proxy, paired views, background comparison, and copy and delete operations. |
| [`src/gui/qfilediff.cpp`](src/gui/qfilediff.cpp) | File selection, background text comparison, aligned table, and staged block edits. |

Keep shared comparison behavior in the library and Qt presentation in the GUI. When changing library behavior, check both callers and preserve the distinction between a file present only on the left (`Removed`) and a file present only on the right (`Added`).

## Directory row alignment

Both `QTreeView` instances share a `QDirDiffModel` through a `QDirDiffProxyModel`. Keep row heights and viewport heights equal across views. The model tracks each path's presence on each side separately from its aggregate diff status. The absent side renders a blank cell that reserves the same row height. Check that filtering, expansion, collapse, and scrolling preserve alignment, especially for one-sided nested branches.

## File comparison

The CLI and GUI share the UTF-8 text loader and Myers diff. Preserve both original line numbers, real blank lines, and the missing-final-newline state through insertions, deletions, and replacements. The file tab reads each side on a worker and maps display rows to blocks in one model. Block edits change the in-memory document, with disk writes through the explicit per-side Save actions. A missing file can be the destination when the directory comparison identified that side as missing.

## Directory file operations

Resolve operations against the roots of the displayed comparison and validate the selected entry on disk. Copy each file using atomic replacement. Capture the selected rows before changing the filesystem, then refresh once after the batch. Report partial failures and keep the deletion confirmation tied to the resolved path. A recursive operation skips symbolic links and junctions.

## Verification

Follow the build commands in [README.md](README.md). The project currently has no registered CTest tests. Use disposable directory fixtures for manual checks and cover the entry points affected by the change.

- **Directory CLI:** compare trees containing identical files, a same-size content change, entries unique to each side, and a file matched by `--ignore`. Check the direction of each marker and the identical count.
- **Directory GUI:** expand and collapse a nested subtree present on only one side. Scroll and toggle filters. Check that the other pane reserves blank rows and that the next shared sibling remains aligned.
- **Text diff:** compare identical files, an empty file against a nonempty file, and a single edit between shared lines. Check hunk context in the CLI and row alignment in the GUI.
- **Text merge:** apply in both directions and inspect the disk before and after Save. Check undo, blank lines, the destination's line endings, and an external edit made before Save.
- **File operations:** try each copy direction, a multi-selection, an existing destination, a failed copy, and a canceled delete. Check resulting files as well as the displayed status.
