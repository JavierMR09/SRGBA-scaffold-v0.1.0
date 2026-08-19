# SRGBA

SRGBA is a clean-room Game Boy Advance emulator project written in C++20. The initial target is
Windows 10/11 x64, with a platform-independent emulation core and an SDL3 desktop frontend.

> **Project status:** foundation scaffold. SRGBA currently opens ROM files, parses their GBA
> cartridge headers, stores frontend settings, and displays a placeholder framebuffer. It does
> **not** execute ARM or Thumb instructions yet.

## What works in this scaffold

- Native, resizable desktop window
- Dear ImGui menu, landing page, game view, and settings panel
- Native `.gba` / `.agb` file dialog
- Cartridge title, game code, maker code, size, fixed-byte, and checksum parsing
- Recent-ROM list and persistent video settings
- Integer scaling plus nearest-neighbor or linear filtering
- Pause, reset, close-ROM, and fullscreen frontend commands
- Isolated, testable `srgba_core` library
- Automated core tests on Linux and full application builds on Windows
- GitHub Actions release ZIP generation

## Windows development setup

Install:

1. Visual Studio 2022 with **Desktop development with C++**
2. Git
3. CMake 3.28 or newer

Dependencies are downloaded at CMake configure time and pinned to known versions:

- SDL 3.4.14
- Dear ImGui 1.92.9b
- nlohmann/json 3.12.0
- Catch2 3.15.3

Configure, build, and test from a Developer PowerShell:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
```

The executable is created under `build/windows-msvc/Debug/`.

For an optimized build and distributable ZIP:

```powershell
cmake --build --preset windows-release
cpack --config build/windows-msvc/CPackConfig.cmake -C Release
```

## Core-only development

The emulator core has no SDL or Dear ImGui dependency. On a system with Ninja and a C++20
compiler:

```bash
cmake --preset core-dev
cmake --build --preset core-dev
ctest --preset core-dev
```

## Project layout

```text
include/srgba/core/   Public, platform-independent core API
src/core/             Cartridge and emulator-core implementation
src/app/              SDL3 and Dear ImGui desktop frontend
tests/                Unit and lifecycle tests
cmake/                Dependency and compiler-warning configuration
docs/                 Architecture, references, and milestone roadmap
.github/workflows/    Continuous integration and tagged releases
```

See [Architecture](docs/ARCHITECTURE.md) and [Roadmap](docs/ROADMAP.md) before implementing new
hardware components.

## ROMs and BIOS files

SRGBA does not contain games, commercial ROMs, Nintendo artwork, encryption keys, or Nintendo's
proprietary GBA BIOS. Users are responsible for supplying legally obtained software. ROM and BIOS
file patterns are excluded from Git by default.

## License

SRGBA is licensed under the [MIT License](LICENSE). Third-party dependencies retain their own
licenses, which are included in packaged releases.

