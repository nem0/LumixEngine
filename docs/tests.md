# Tests

This document explains how tests are organized and how to run them locally.

## Overview

Lumix currently has two test paths:

- `tests.exe` for headless/unit-style tests (built from `src/tests`)
- ImGui Test Engine integration in `studio.exe` for interactive editor UI tests

Main suites registered in `src/tests/main.cpp`:

- Particle script tokenizer tests
- Particle script compiler tests
- Particle script collector tests
- UI tokenizer tests
- UI parser/runtime tests
- UI style tests
- UI layout tests

## Run All Headless Tests

From the repository root:

```bat
scripts\run_tests.bat
```

The script:

1. Generates solution files with tests enabled (`genie.exe --with-tests vs2022`).
2. Builds `tmp\vs2022\LumixEngine.sln` in `Debug|x64`
3. Runs `tmp\vs2022\bin\Debug\tests.exe`

Result handling:

- Exit code `0`: all tests passed
- Exit code `1`: one or more tests failed (`tests.exe` currently returns `1` for any failure, not the number of failed tests)
- Exit code `2`: test executable was not found

## Run ImGui Editor Tests

From the repository root, run:

```bat
scripts\run_imgui_tests.bat
```

This script:

1. Generates/builds Studio with test support
2. Launches `studio.exe` with ImGui Test Engine UI enabled

Arguments:

- Optional first argument: build configuration (`Debug` by default).
- Supported configuration values: `Debug` and `RelWithDebInfo`.
- All other arguments are passed through to `studio.exe`.
- If the first argument is not one of the configuration values, it is treated as a Studio argument.

```bat
scripts\run_imgui_tests.bat Debug
scripts\run_imgui_tests.bat RelWithDebInfo
scripts\run_imgui_tests.bat RelWithDebInfo -open "data/scripts/tests/sample.unv"
scripts\run_imgui_tests.bat -open "data/scripts/tests/sample.unv"
```

- `-imgui_test_ui` enables ImGui Test Engine windows in Studio, where editor UI tests can be browsed and executed.

Example launch form:

- `studio.exe -imgui_test_ui -data_dir <repo>\data ...`

## CI Behavior

GitHub Actions workflow `.github/workflows/particle-tests.yml` runs:

- `scripts\run_tests.bat`

It currently triggers on pushes and pull requests when files matching `**/particle_script*` change.
