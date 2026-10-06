# SRGBA Roadmap

## M0 - Desktop foundation (complete)

- [x] C++20 and CMake project
- [x] SDL3 window and renderer
- [x] Dear ImGui application shell
- [x] ROM header loading and validation
- [x] Placeholder 240x160 framebuffer
- [x] Settings and recent files
- [x] Core tests, Windows CI, and release packaging

## M1 - ARM7TDMI foundation (complete)

- [x] Physical and banked register model
- [x] CPSR and SPSR representation
- [x] Processor mode transitions
- [x] ARM condition evaluation and barrel shifter
- [x] ARM decoder and base data-processing instructions
- [x] Thumb decoder and base ALU instructions
- [x] Branch, branch-with-link, and branch-exchange pipeline behavior
- [x] Exceptions and interrupt entry
- [x] Instruction-vector test harness

## M2 - GBA bus and boot (complete)

- [x] BIOS, EWRAM, IWRAM, IO, palette, VRAM, OAM, and Game Pak regions
- [x] Mirroring, access width, alignment, and open-bus behavior
- [x] WAITCNT and sequential/non-sequential Game Pak timing
- [x] User-supplied BIOS loading and validation
- [x] Post-BIOS development boot path
- [x] First CPU-focused homebrew test ROM

## M3 - Scheduling and basic video (complete)

- [x] Master-cycle scheduler
- [x] Scanline, HBlank, VBlank, and VCount timing
- [x] Interrupt controller
- [x] Keypad registers and frontend mapping
- [x] Bitmap modes 3, 4, and 5
- [x] First interactive homebrew output (bundled `samples/SRGBA-demo.gba`)
- [x] ARM PSR transfer, multiply, long multiply, swap, and user-bank block transfers
- [x] Built-in replacement BIOS with HLE software interrupts

## M4 - DMA, timers, and complete PPU (complete)

- [x] Four DMA channels and trigger timing
- [x] Four hardware timers and cascading
- [x] Regular and affine backgrounds (including affine BG2 in bitmap modes)
- [x] Sprites and object attributes
- [x] Windows, blending, mosaic, and layer priority
- [x] PPU regression suite
- [x] Game Pak prefetch buffer timing (approximation)
- [x] Two-stage CPU prefetch pipeline; public ARM/Thumb/memory/BIOS/NES suites pass in CI
- [x] Second demo ROM exercising DMA, timers, tiles, sprites, windows, and blending

Deferred: per-line sprite rendering cycle limits, DMA3 video-capture mode, and cycle-exact BIOS
call timing (the HLE BIOS returns correct results but not Nintendo's exact cycle counts).

## M5 - Audio and cartridge persistence (complete)

- [x] PSG square, wave, and noise channels
- [x] Direct Sound FIFOs and DMA integration
- [x] SDL audio queue and resampling
- [x] SRAM, Flash, and EEPROM save media
- [x] Automatic, atomic battery saves
- [x] Public save-chip suites (SRAM, Flash 64K/128K, none) in CI
- [x] HLE MidiKey2Freq and SoundBias BIOS calls; demo sound effects

Deferred: the BIOS MusicPlayer2000 sound-driver calls (games normally link their own driver),
Atmel flash page writes, and real-time-clock cartridges.

## M6 - Emulator features (complete)

- [x] Versioned save states with nine quick slots per game (and rollback on damaged states)
- [x] Fast-forward (2x-8x or unlimited), frame advance, and rewind
- [x] Keyboard and gamepad remapping with two bindings per button
- [x] Cheat engine: raw, CodeBreaker, GameShark / Action Replay v1-v2, and Action Replay v3
- [x] ROM folder scanning with header and save-chip metadata, search, and drag and drop
- [x] GBA LCD color correction and LCD grid / scanline screen filters

Deferred: cheat codes that change the encryption key (DEADFACE), encrypted CodeBreaker codes,
GameShark button-activated codes, Action Replay v3 slide and IO-register codes, remappable
hotkeys, and zipped ROMs in the library.

## M7 - Compatibility and 1.0 (next)

- [ ] Public CPU and timing suites
- [ ] Representative homebrew and commercial-game compatibility matrix
- [ ] Performance profiling and safe optimizations
- [ ] Crash reporting guidance and issue templates
- [ ] Stable save-state policy
- [ ] Signed, reproducible Windows release artifacts
