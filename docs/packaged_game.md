# Packaging a game

The editor's **Export game** dialog copies everything a game needs to run outside the editor into a destination directory. The result is either a folder or, with **Pack data** enabled, a single `main.pak` file.

## Steps

1. Open the project in the editor.
2. Open the **Export game** window from the editor menu.
3. Click the **Destination dir** button and choose an empty folder for the build.
4. Set **Executable name** to the desired filename, including `.exe` (defaults to `app.exe`). The editor remembers this setting.
5. Optionally tick **Include debug symbols (.pdb)** to copy `app.pdb` alongside the executable, keeping the name `app.pdb` even when the executable is renamed. This is off by default.
6. Optionally tick **Pack data** to put all game data into `main.pak` instead of loose files. **Generate pak_list.txt** controls whether the pack's file listing is written and is on by default. Both options are remembered.
7. Click **Export**. The editor compiles all resources first and shows "Compiling resources..." until it finishes. You can press **Cancel** to stop the export at that point.
8. When the export is done, the button changes to "Export finished".

## What gets exported

- `lumix.prj`, the project file, saved fresh from the editor.
- Compiled resources from `.lumix/resources/<hash>.res`. Only resources registered with the asset compiler are included.
- Everything under `engine/shaders/` and `maps/`.
- Plugin data. Each GUI plugin's `exportData()` is called with the destination directory at the end of the export.
- `app.exe`, `dbghelp.dll` and `dbgcore.dll` from `bin/`, or from the executable's directory if `bin/app.exe` does not exist. The exported `app.exe` uses the chosen **Executable name**.

Folders containing the `AssetCompiler::IGNORE_MARKER` file are skipped. Hidden files and folders (starting with `.`), the `bin` folder, `main.pak` and `error.log` are never exported.

## Packed or loose

- **Pack data off:** the compiled files are copied as loose files, keeping their relative paths, into the destination directory.
- **Pack data on:** all files go into `main.pak`. The file starts with a header of the file count, followed by one entry per file (hash, offset and size), and then the file contents.

## Running the packaged game

The packaged `app.exe` looks for `main.pak` next to it at startup. If the file exists, the game reads everything from the pack. If not, it uses the folder it is in as the data directory, so loose files work without a pack.

The data directory can be overridden with the `-data_dir <path>` command-line argument.

Resources need to be compiled before they can be packaged. The export compiles them for you, so this usually happens without any action on your part.

## Related

- [ImGui in packaged game](app_imgui.md): how to enable the editor-style ImGui overlay in a packaged game.
