# Editor Command Line Options

This document lists command line options supported by the **Lumix Studio editor** (`studio` executable).

## Syntax

- Flags use a single dash, for example: `-workers 8`
- Values containing spaces should be quoted: `-data_dir "C:/My Project/"`
- Multiple options can be combined.

## Options

### `-data_dir <path>`

Sets the project data directory and starts the editor directly in that project.

- Expected value: path to project root directory
- Example: `-data_dir "C:/projects/MyGame/"`
- Notes:
  - If valid, the welcome screen is skipped.
  - `-open` is only processed when `-data_dir` is present.

### `-open <path_to_world.unv>`

Opens a world file on startup.

- Expected value: path to `.unv` world file
- Example: `-open "maps/main.unv"`
- Notes:
  - Works only together with `-data_dir`.

### `-workers <count>`

Overrides worker thread count used by the job system.

- Expected value: unsigned integer
- Example: `-workers 12`
- Default behavior when omitted: uses CPU count (capped to 64).

### `-plugin <name_or_path>`

Loads an additional system/plugin library at startup.

- Expected value:
  - Plugin name (resolved by plugin manager), or
  - Full library path (contains `.`), e.g. `my_plugin.dll` / `my_plugin.so`
- Example:
  - `-plugin renderer`
  - `-plugin "C:/build/custom_plugin.dll"`
- Notes:
  - Can be provided multiple times.

### `-profile_start`

Takes a profiler snapshot automatically when the editor starts.

- Expected value: none (flag)

### `-profile_cswitch` (Windows)

Enables profiler context-switch tracing (ETW-based path in profiler code).

- Expected value: none (flag)
- Platform: Windows-specific implementation

### `-no_crash_report`

Disables crash reporting.

- Expected value: none (flag)
