# SKEIN Engine

SKEIN is a modular, high-performance C++ game engine for Windows x64, built around large worlds, modern explicit graphics APIs, aggressive multithreading, and a custom editor workflow.

The project is currently in its foundation stage.

## Current Status

The repository contains the initial compiling project skeleton, including:

- SKEIN Runtime
- SkeinEditor
- SkeinInsights
- SkeinUI
- SkeinTrace
- Foundation, Platform, and Core modules
- Smoke tests
- MSVC and Clang toolchains
- Debug and Release configurations

## Target Platform

Initial platform support:

- Windows 10 / 11
- x64 only

Planned later:

- Linux x64
- Headless Linux server support for Starcore

## Toolchain

SKEIN currently supports four CMake presets:

| Preset | Compiler | Configuration |
|---|---|---|
| `msvc-debug` | MSVC | Debug |
| `msvc-release` | MSVC | Release |
| `clang-debug` | clang-cl | Debug |
| `clang-release` | clang-cl | Release |

All presets use:

- C++20
- CMake
- Ninja
- Dynamic MSVC runtime libraries
- SDL3 3.4.10, fetched and built statically by CMake

## Requirements

Install the following before building:

- Visual Studio with Desktop Development for C++
- MSVC x64 build tools
- LLVM / clang-cl
- CMake 3.25 or newer
- Ninja
- Git

Run commands from a Visual Studio Developer Command Prompt, or another terminal with the MSVC environment configured.

## Configure

Configure an individual preset:

```bat
cmake --preset msvc-debug
```

Available presets:

```bat
cmake --preset msvc-debug
cmake --preset msvc-release
cmake --preset clang-debug
cmake --preset clang-release
```

Configure all presets:

```bat
Scripts\configure-all.bat
```

## Build

Build an individual preset:

```bat
cmake --build --preset msvc-debug
```

Build all configurations:

```bat
Scripts\build-all.bat
```

## Test

Run tests for an individual preset:

```bat
ctest --preset msvc-debug
```

Run all test configurations:

```bat
Scripts\test-all.bat
```

## Executables

The initial project produces the following executables:

| Target | Purpose |
|---|---|
| `SkeinRuntime` | Standalone engine runtime |
| `SkeinEditor` | SKEIN game editor |
| `SkeinInsights` | Trace and performance analysis application |
| `SkeinSmokeTests` | Basic engine startup and shutdown tests |

## Current Modules

| Module | Responsibility |
|---|---|
| `SkeinFoundation` | Core types, logging, utilities, and shared low-level functionality |
| `SkeinPlatform` | Windows platform abstraction with SDL3 windowing |
| `SkeinCore` | Engine lifecycle and high-level coordination |
| `SkeinUI` | Native retained-mode UI framework |
| `SkeinTrace` | Runtime tracing foundation for SkeinInsights |

## Third-Party Dependencies

| Dependency | Version | Licence | Use |
|---|---:|---|---|
| [SDL](https://github.com/libsdl-org/SDL) | 3.4.10 | zlib | Platform window creation and lifecycle |

## Planned Architecture

Major planned systems include:

- Custom Render Hardware Interface
- Vulkan backend
- Direct3D 12 backend
- Forward+ renderer
- Deferred renderer
- Render graph
- Large World Coordinates
- Memory system
- Job system
- Reflection and serialization
- Asset pipeline
- Scene and world streaming
- Jolt Physics
- SkeinScript
- Plugin framework
- Audio
- Animation
- Navigation
- Networking
- SkeinInsights
- SkeinUI

## Repository Layout

```text
SKEINEngine/
├── CMake/
├── Scripts/
├── Source/
│   ├── Core/
│   ├── Editor/
│   ├── Foundation/
│   ├── Insights/
│   ├── Platform/
│   ├── Runtime/
│   └── UI/
├── Tests/
├── CMakeLists.txt
├── CMakePresets.json
└── README.md
```

Generated files are kept outside the source tree:

```text
Build/
Install/
```

These directories are ignored by Git.

## Development Rules

- `main` must compile under all four supported presets.
- Engine systems should be implemented as separate modules.
- Platform-specific code must remain inside platform implementations.
- New systems must define ownership, threading, memory, and plugin boundaries.
- Feature scope should remain aligned with actual SKEIN and Starcore requirements.
- Warnings are treated as errors.

## Git Setup

Initialise the repository:

```bat
git init
git branch -M main
git add .
git commit -m "Initial SKEIN Engine project skeleton"
```

Add a remote:

```bat
git remote add origin YOUR_REPOSITORY_URL
git push -u origin main
```

## Documentation

The current engine architecture is defined in:

`SKEIN-SPEC-001 Engine Master Specification`

Subsystem specifications will be added as implementation begins.

## Licence

Copyright © Coding Custard Studios.

All rights reserved.

The source code is currently proprietary and may not be copied, redistributed, or used outside Coding Custard Studios without permission.
