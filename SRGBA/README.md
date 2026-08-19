# SRGBA

SRGBA is a clean-room Game Boy Advance emulator project written in C++20. The initial target is
Windows 10/11 x64, with a platform-independent emulation core and an SDL3 desktop frontend.

> **Project status:** M1 ARM7TDMI foundation. SRGBA can execute isolated ARM data-processing and
> Thumb ALU instruction vectors, model processor modes and exceptions, open ROM files, and display
> a placeholder framebuffer. The CPU is not connected to a GBA memory bus yet, so games do not
> boot in this milestone.

## What works in this scaffold

- Native, resizable desktop window
- Dear ImGui menu, landing page, game view, and settings panel
- Native `.gba` / `.agb` file dialog
- Cartridge title, game code, maker code, size, fixed-byte, and checksum parsing
- Recent-ROM list and persistent video settings
- Integer scaling plus nearest-neighbor or linear filtering
- Pause, reset, close-ROM, and fullscreen frontend commands
- ARM7TDMI physical and banked registers, CPSR, and five SPSRs
- ARM condition evaluation and specification-accurate barrel-shifter edge cases
- Base ARM data-processing and Thumb ALU instruction execution
- ARM/Thumb branches, branch-with-link, branch-exchange, and exception entry/return
- Isolated, testable `srgba_core` library
- Automated core tests on Linux and full application builds on Windows
- GitHub Actions release ZIP generation

## Windows development setup

Install:

1. Visual Studio 2026 with **Desktop development with C++**
2. Git
3. CMake 4.2 or newer

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
src/core/             ARM7TDMI, cartridge, and emulator-core implementation
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
