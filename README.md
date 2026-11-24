# Directory Diff

Directory Diff is a C++20 utility for comparing two directory trees. Its side-by-side Qt interface is inspired by Araxis Merge's folder comparison. A command-line interface uses the same comparison engine.

The directory comparison scans regular files recursively and matches them by relative path. It compares file sizes first, then hashes equal-sized pairs with xxHash. The GUI displays the results in two trees and can filter identical, modified, left-only, and right-only entries.

The panes share a vertical layout. A file or folder present on only one side leaves a blank cell on the other side, so matching paths stay on the same line.

## Build on Windows

Use a C++20 compiler, a matching Qt 6 kit with Core, Gui, and Widgets, and CMake. The example uses MSVC, Ninja, and a Qt MSVC kit. CMake fetches [`utils-log`](https://github.com/nesall/utils-log) through Git. Qt is required at configure time even when building only the CLI target.

From an x64 Visual Studio developer PowerShell with `cmake` and `ninja` on `PATH`, run these commands from the repository root. Set `QT6_DIR` to the root of your installed Qt kit.

```powershell
$env:QT6_DIR = 'C:\Qt\6.11.2\msvc2022_64'
cmake -S . -B build/release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release
cmake --build build/release
```

For an existing local `utils-log` checkout, add `-DFETCHCONTENT_SOURCE_DIR_UTILS_LOG=C:/src/utils-log` to the configure command and substitute its path. The Release build was verified with Visual Studio 2026 and Qt 6.11.2 using a local checkout.

## GUI

```powershell
$env:PATH = "$env:QT6_DIR\bin;$env:PATH"
.\build\release\directory_diff_gui.exe
```

1. In the **Directory Diff** tab, enter or browse for the left and right folders.
2. Click **Compare**. Use the toolbar filters to choose which result categories to show.
3. For a file present on both sides, right-click its row and choose **Compare files** to open the text diff tab.

Open **Settings** to save comparison exclusions. Enter one pattern per line, such as `*.tmp` for temporary files, `build/` for folders named `build` at any depth, or `third_party/cache/` for that folder relative to each comparison root. Saved rules apply to both panes on the next comparison and persist across sessions.

For a standalone text comparison, select **New → File Diff**, choose both files, then click **Compare** or press **Enter** in a path field. Use **Previous Change** and **Next Change** to move between change blocks. Each side shows its own line numbers.

Select a changed row and choose **Apply to Left** or **Apply to Right** to copy that block into the destination. Use **Undo** to reverse an edit. **Save Left** and **Save Right** write their respective files and check for changes made on disk since the comparison or last save. A file opened from a one-sided directory result can be created by applying a block and then saving it.

## CLI

```powershell
.\build\release\directory_diff_cli.exe --help
.\build\release\directory_diff_cli.exe dir-diff 'C:\src\left' 'C:\src\right' --ignore '*.tmp;*.obj'
.\build\release\directory_diff_cli.exe file-diff 'C:\src\left\main.cpp' 'C:\src\right\main.cpp'
```

`dir-diff` lists `[+]` for right-only files, `[-]` for left-only files, and `[~]` for modified files. It reports the identical-file count separately. Quote the semicolon-separated value passed to `--ignore`. It accepts the same file and folder patterns as the GUI.

`file-diff` compares UTF-8 text line by line and prints unified-style output. The GUI and CLI compare line contents independently of the line-ending style or UTF-8 BOM, and report a missing final newline as a change.

## Known limitations

- **Empty directories.** A directory appears in the result tree when the scan reports at least one file below that path on either side.
- **File comparison.** The viewer supports UTF-8 text. Very dissimilar regions can appear as one change block once the diff reaches its trace memory limit.
- **File operations.** The directory view has directional copy and delete actions. Copying over an existing file removes the destination before attempting the replacement, and recursive copies can report success despite individual failures. Use disposable directories when trying these actions.

Development notes and verification cases are in [AGENT.md](AGENT.md).
