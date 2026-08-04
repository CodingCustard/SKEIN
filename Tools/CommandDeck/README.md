# SKEIN Command Deck v1.4

SKEIN Command Deck opens the current Windows x64 development shells inside one Windows Terminal workspace.

## Project alignment

This version is aligned with the CMake project skeleton and its four presets:

- `msvc-debug`
- `msvc-release`
- `clang-debug`
- `clang-release`

Linux and WSL panes have been removed from the active profiles because Linux support is currently deferred. The internal WSL support remains available for a future Starcore server profile.

Build Forge and its dependency on `Tools\SkeinBuild.py` have been replaced by a CMake build shell using the Visual Studio developer environment.

## Installation

Copy the package contents into the root of your SKEIN repository, preserving the included paths.

## Profiles

- `full`: CMake Build, Git Bash, VS Developer
- `build`: CMake Build, VS Developer
- `git`: Git Bash, VS Developer

## First checks

```powershell
Set-Location <path-to-your-SKEIN-repository>
py .\Tools\SkeinCommandDeck.py paths
py .\Tools\SkeinCommandDeck.py check --all --verbose
py .\Tools\SkeinCommandDeck.py print full
```

## Open the full workspace

```powershell
py .\Tools\SkeinCommandDeck.py open full
```

Or launch the interactive menu:

```powershell
py .\Tools\SkeinCommandDeck.py
```

## Build commands

The CMake pane starts inside the Visual Studio x64 developer environment and prints the available presets.

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
```

Use `--no-startup` to suppress the preset guide:

```powershell
py .\Tools\SkeinCommandDeck.py open full --no-startup
```

## Tests

Build and run the default `msvc-debug` test preset directly from Command Deck:

```powershell
py .\Tools\SkeinCommandDeck.py test
```

Choose one preset, run all presets, or skip the configure/build step:

```powershell
py .\Tools\SkeinCommandDeck.py test clang-release
py .\Tools\SkeinCommandDeck.py test --all
py .\Tools\SkeinCommandDeck.py test msvc-debug --no-build
```

The interactive menu also includes **Build and run MSVC Debug tests**.

### Command Deck unit tests

```powershell
py -m unittest discover -s .\Tests\Tools\CommandDeck -p "test_*.py"
```

Command Deck does not alter Windows Terminal settings, install software, or terminate shells after launch.


## Repository discovery

`repositoryRoot` is `null` by default. Command Deck discovers the repository from the current directory or the location of `Tools/SkeinCommandDeck.py` by locating both `CMakePresets.json` and the `Tools` directory.

Set an explicit path only for an unusual installation:

```json
"repositoryRoot": "D:\\Projects\\SKEINEngine"
```

## Git Bash selection

Command Deck v1.3 explicitly selects Git for Windows Bash rather than the first `bash.exe` found on `PATH`. The repository path is converted inside Git Bash with `cygpath`, so drive letters and spaces are handled by the shell that actually owns the mount convention.
