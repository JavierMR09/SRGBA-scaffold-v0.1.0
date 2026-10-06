# SRGBA

SRGBA is a clean-room Game Boy Advance emulator project written in C++20. The initial target is
Windows 10/11 x64, with a platform-independent emulation core and an SDL3 desktop frontend.

> **Project status:** M6 emulator features. SRGBA emulates the ARM7TDMI (passing the public ARM,
> Thumb, memory, BIOS, pipeline, and save-chip test suites), every video mode with sprites and
> effects, DMA, timers, all six sound channels, and SRAM/Flash/EEPROM saves, with no BIOS dump
> required. It adds save states, fast-forward, rewind, cheats, control remapping, a ROM library,
> and LCD screen filters. M7 focuses on game compatibility on the way to 1.0.

## Try it

Download the latest `SRGBA-windows-x64` ZIP, extract it anywhere, and run `SRGBA.exe`. The
`samples` folder has two original demos:

- `SRGBA-demo.gba`: move the square with the arrow keys (or a controller's D-pad) and hold **X**
  (GBA A) to change its color.
- `SRGBA-tiles-demo.gba`: a scrolling tile background, a ball sprite with a see-through shadow,
  and a highlight band that sweeps across the screen. Press **X** (GBA A) for a chirp and **Z**
  (GBA B) for a noise burst. It exercises DMA, timers, tile graphics, sprites, windows, blending,
  and sound together.

Game progress is saved automatically to a `.sav` file next to the ROM (the same format other
emulators use, so saves can move between them).

### Playing

- **Library:** choose **Add ROM folder** (or drop a folder on the window) and SRGBA lists every
  `.gba` file in it with its title, game code, and save type. Search it and double-click to play.
  You can also drop a ROM on the window or onto `SRGBA.exe`.
- **Save states:** nine slots per game. **Shift+F1-F9** saves, **F1-F9** loads, and the **States**
  menu shows when each slot was saved.
- **Speed:** hold **Tab** to fast-forward (2x-8x or as fast as possible, set in **View >
  Settings > Emulation**), hold **`** (the key left of 1) to rewind, press **N** to step one frame
  while paused, and **Space** to pause. On a gamepad, hold the right trigger to fast-forward and
  the left trigger to rewind.
- **Cheats:** **Tools > Cheats** accepts raw `address:value` codes, CodeBreaker codes, and
  GameShark / Action Replay codes (v1-v2 and v3, encrypted as printed). Automatic mode detects the
  format. Each game's cheats are kept in a `.cht` file beside it, and libretro cheat files work too.
- **Looks:** **View > GBA LCD colors** recreates the original screen's colors, and **View > Screen
  filter** adds an LCD grid or scanlines.

### Controls

Every button can be remapped (two keys and two gamepad buttons each) in **View > Settings >
Controls**. The defaults are:

| GBA      | Keyboard                 | Controller                     |
|----------|--------------------------|--------------------------------|
| A        | X                        | A / Cross (bottom face button) |
| B        | Z                        | B / Circle (right face button) |
| L / R    | A / S                    | Left / right bumper            |
| Start    | Enter                    | Start / Menu                   |
| Select   | Backspace or Right Shift | Back / View                    |
| D-pad    | Arrow keys               | D-pad or left stick            |

Other hotkeys: **Ctrl+O** opens a ROM, **Ctrl+R** resets, **M** mutes, and **F11** toggles
fullscreen.

## What works in this scaffold

- Native, resizable desktop window
- Dear ImGui menu, landing page with library, game view, settings, and cheat editor
- Native `.gba` / `.agb` file dialog
- Cartridge title, game code, maker code, size, fixed-byte, and checksum parsing
- Recent-ROM list and persistent video settings
- Integer scaling plus nearest-neighbor or linear filtering
- Pause, reset, close-ROM, and fullscreen frontend commands
- ARM7TDMI physical and banked registers, CPSR, and five SPSRs
- ARM condition evaluation and specification-accurate barrel-shifter edge cases
- Base ARM data-processing and Thumb ALU instruction execution
- ARM/Thumb branches, branch-with-link, branch-exchange, and exception entry/return
- BIOS, EWRAM, IWRAM, IO, palette, VRAM, OAM, and three Game Pak ROM windows
- GBA memory mirroring, ARM7TDMI alignment rotation, open-bus latching, and video byte-write rules
- WAITCNT-controlled sequential and non-sequential Game Pak access timing
- ARM single, halfword/signed, and block data transfers
- Thumb PC/SP-relative, register/immediate, signed, stack, and multiple data transfers
- Cycle-counted CPU instruction fetch through the GBA bus
- Validated 16 KiB user BIOS loading with protected reads outside BIOS execution
- Default post-BIOS direct boot for legally distributed homebrew without a proprietary BIOS
- Generated CPU-focused test ROM that executes code from Game Pak and writes results to EWRAM
- ARM MRS/MSR, MUL/MLA, long multiplies, SWP/SWPB, and exception-return block transfers
- Deterministic master-cycle scheduler with HALT fast-forwarding
- Scanline, HBlank, VBlank, and VCount timing with DISPSTAT/VCOUNT registers
- Interrupt controller (IE, IF, IME) and IRQ delivery through the BIOS dispatcher
- Keypad registers and keypad interrupts, mapped to keyboard and SDL gamepads
- Bitmap video modes 3, 4 (with page flipping), and 5, forced blank, and backdrop color
- Built-in replacement BIOS: original IRQ dispatcher plus high-level emulation of the common BIOS
  calls (VBlankIntrWait, IntrWait, Halt, Div, Sqrt, ArcTan2, CpuSet, CpuFastSet, LZ77, Huffman,
  run-length, BitUnPack, affine setup, and more)
- Frame pacing at the GBA's native ~59.73 Hz, independent of monitor refresh rate
- An original demo ROM, generated at build time and shipped in `samples/`
- Two-stage ARM7TDMI prefetch pipeline (self-modifying code and BIOS open-bus values behave as on
  hardware) and an approximate Game Pak prefetch buffer
- Video modes 0-2: regular tile backgrounds (4bpp/8bpp, flips, scrolling, all map sizes) and
  affine backgrounds with wrap-around, plus affine transformation of bitmap modes 3-5
- 128 sprites: regular and affine, double-size, 1D/2D tile mapping, flips, priorities,
  semi-transparency, and the object window
- Windows 0/1, object window, alpha blending, brightness effects, and mosaic
- Four DMA channels (immediate, VBlank, HBlank, repeat, address modes, IRQs) with CPU stall timing
- Four hardware timers with prescalers, cascading, and interrupts
- Public CPU test suites (jsmolka/gba-tests) run in CI
- Sound: two square channels (with sweep), the wave channel (both banks), noise, and the two
  Direct Sound FIFOs fed by timers and DMA, mixed to 32,768 Hz stereo and played through SDL
- Save chips detected from the cartridge's SDK tag: 32 KiB SRAM, 64/128 KiB Flash (IDs, erase,
  program, bank switching), and 512 B/8 KiB EEPROM over DMA
- Automatic battery saves: written half a second after the game stops writing, on ROM close, and
  on exit, through a temporary file and rename so a crash never corrupts a save
- Versioned save states with nine quick slots, validated on load and rolled back on failure
- Rewind history stored as compressed differences under a memory budget, fast-forward, and frame
  advance
- Remappable keyboard and gamepad controls with two bindings per GBA button
- Cheat engine for raw, CodeBreaker, GameShark / Action Replay v1-v2, and Action Replay v3 codes,
  including ROM patches, with per-game libretro-compatible `.cht` files
- ROM library with background folder scanning, header and save-chip metadata, and search
- GBA LCD color correction and LCD grid / scanline filters drawn at the screen's resolution
- Drag-and-drop and command-line ROM loading
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
src/core/             ARM7TDMI, bus, scheduler, PPU, HLE BIOS, and emulator-core implementation
src/app/              SDL3 and Dear ImGui desktop frontend
tests/                Unit, instruction, hardware, and integration tests
tools/homebrew/       Small ARM assembler and the original SRGBA demo ROM
cmake/                Dependency and compiler-warning configuration
docs/                 Architecture, references, and milestone roadmap
.github/workflows/    Continuous integration and tagged releases
```

See [Architecture](docs/ARCHITECTURE.md) and [Roadmap](docs/ROADMAP.md) before implementing new
hardware components.

## ROMs and BIOS files

SRGBA does not contain games, commercial ROMs, Nintendo artwork, Nintendo encryption keys, or
Nintendo's proprietary GBA BIOS. Users are responsible for supplying legally obtained software. ROM and BIOS
file patterns are excluded from Git by default.

Direct boot is enabled by default and initializes the CPU, stack banks, and minimum post-BIOS IO
state before starting at `0x08000000`. Without a BIOS file, SRGBA uses its own small replacement
system ROM and implements BIOS calls in C++; none of it is derived from Nintendo's BIOS. To use your own BIOS, choose **File > Load BIOS**, select an
exactly 16 KiB image, then choose **Use loaded BIOS** in Settings. SRGBA stores only the path in its
local settings; the BIOS is never copied into a build or release package.

## License

SRGBA is licensed under the [MIT License](LICENSE). Third-party dependencies retain their own
licenses, which are included in packaged releases.
