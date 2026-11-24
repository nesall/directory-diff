# Directory Diff

Directory Diff is a C++20 utility for comparing two directory trees. Its side-by-side Qt interface is inspired by Araxis Merge's folder comparison. A command-line interface uses the same comparison engine.

The directory comparison scans regular files and folders, including empty folders, and matches them by relative path. It compares file sizes first, then hashes equal-sized pairs with xxHash. The GUI displays the results in two trees and can filter identical, modified, left-only, and right-only entries.

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
2. Click **Compare**. The tab shows progress as it scans entries and compares file contents. Click **Cancel** to stop and discard the comparison. Use the toolbar filters to choose which result categories to show.
3. For a file present on both sides, right-click its row and choose **Compare files** to open the text diff tab.

After a comparison, matching folders directly under the compared roots start collapsed. Other branches open automatically. Expand a folder in either pane to open it in both.

**Stats** shows scan totals and a breakdown of differences for the active directory tab after a successful comparison. File and folder totals honor exclusions and include results hidden by the display filters.

Open **Settings** to save comparison exclusions. Enter one pattern per line, such as `*.tmp` for temporary files, `build/` for folders named `build` at any depth, or `third_party/cache/` for that folder relative to each comparison root. Saved rules apply to both panes on the next comparison and persist across sessions.

In the directory view, use the context menu to copy selected entries between the compared roots or permanently delete an entry from one side. Copy and delete run in the background. The tab shows the current path and completed entry count. **Cancel** stops the remaining work and keeps completed changes. The status bar reports skipped entries, and the comparison refreshes after the operation.

Use **Copy Name**, **Copy Full Path**, or **Copy Relative Path** to put the clicked entry’s name or path on the clipboard. Paths follow the side you clicked.

Only one copy, delete, or File Diff save can write to disk at a time. You can keep browsing or comparing in other tabs while a directory operation runs.

Folder copies merge into the destination. An existing file is replaced only when its contents still match the comparison. On Windows, case aliases are paired where the filesystem resolves them to the same entry, and copy preserves the destination spelling.

Folder operations skip symbolic links and junctions. Copy can create excluded descendants, and it skips existing files excluded from the comparison because their contents could not be verified. Delete asks for confirmation and covers all descendants, including exclusions.

For a standalone text comparison, select **New → File Diff**, choose both files, then click **Compare** or press **Enter** in a path field. Use **Previous Change** and **Next Change** to move between change blocks. Each side shows its own line numbers.

Select a changed row and choose **Apply to Left** or **Apply to Right** to copy that block into the destination. Use **Undo** to reverse an edit. **Save Left** and **Save Right** write their respective files and check for changes made on disk since the comparison or last save. A file opened from a one-sided directory result can be created by applying a block and then saving it.

## CLI

```powershell
.\build\release\directory_diff_cli.exe --help
.\build\release\directory_diff_cli.exe dir-diff 'C:\src\left' 'C:\src\right' --ignore '*.tmp;*.obj'
.\build\release\directory_diff_cli.exe file-diff 'C:\src\left\main.cpp' 'C:\src\right\main.cpp'
```

`dir-diff` lists `[+]` for right-only entries, `[-]` for left-only entries, and `[~]` for changed files or file/folder conflicts. Folder paths have a trailing `/`. It reports the identical-file count separately. Quote the semicolon-separated value passed to `--ignore`. It accepts the same file and folder patterns as the GUI.

`file-diff` compares UTF-8 text line by line and prints unified-style output. The GUI and CLI compare line contents independently of the line-ending style or UTF-8 BOM, and report a missing final newline as a change.

## Known limitations

- **File comparison.** The viewer supports UTF-8 text. Very dissimilar regions can appear as one change block once the diff reaches its trace memory limit.

Development notes and verification cases are in [AGENT.md](AGENT.md).
