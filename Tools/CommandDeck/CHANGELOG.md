# Changelog

## v1.4

- Added a native `test` command that configures, builds, and runs a selected CMake test preset.
- Added `test --all` for the complete preset matrix and `--no-build` for CTest-only runs.
- Added an interactive-menu action for the default `msvc-debug` tests.
- Added a Visual Studio developer-environment test launcher with exit-code propagation.

## v1.3

- Fixed Git pane selection so Git for Windows Bash is preferred over generic `bash.exe` shims on `PATH`.
- Git pane startup now uses `cygpath` to translate the discovered native repository path.
- Prevents WSL or WindowsApps Bash from receiving Git Bash-style paths such as `/e/SKEINEngine`.

# SKEIN Command Deck Changelog

## v1.2

- Removed the hard-coded `E:\SKEINEngine` repository root.
- Enabled automatic repository discovery from the installed tool location.
- Updated setup instructions to support any local drive or repository folder.

# Changelog

## v1.1.0

- Replaced the obsolete Build Forge pane with a CMake build pane.
- Removed the dependency on `Tools/SkeinBuild.py`.
- Added direct awareness of the project `CMakePresets.json`.
- Updated the active workspace profiles for Windows-only development.
- Removed Linux and WSL panes from active profiles while retaining internal WSL support for future Starcore work.
- Updated startup guidance for the four supported presets: MSVC Debug/Release and clang-cl Debug/Release.
- Updated tests and documentation.
