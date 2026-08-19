# SRGBA Roadmap

## M0 - Desktop foundation (current)

- [x] C++20 and CMake project
- [x] SDL3 window and renderer
- [x] Dear ImGui application shell
- [x] ROM header loading and validation
- [x] Placeholder 240x160 framebuffer
- [x] Settings and recent files
- [x] Core tests, Windows CI, and release packaging

## M1 - ARM7TDMI foundation

- [ ] Physical and banked register model
- [ ] CPSR and SPSR representation
- [ ] Processor mode transitions
- [ ] ARM condition evaluation and barrel shifter
- [ ] ARM decoder and base data-processing instructions
- [ ] Thumb decoder and base ALU instructions
- [ ] Branch, branch-with-link, and branch-exchange pipeline behavior
- [ ] Exceptions and interrupt entry
- [ ] Instruction-vector test harness

## M2 - GBA bus and boot

- [ ] BIOS, EWRAM, IWRAM, IO, palette, VRAM, OAM, and Game Pak regions
- [ ] Mirroring, access width, alignment, and open-bus behavior
- [ ] WAITCNT and sequential/non-sequential Game Pak timing
- [ ] User-supplied BIOS loading and validation
- [ ] Post-BIOS development boot path
- [ ] First CPU-focused homebrew test ROM

## M3 - Scheduling and basic video

- [ ] Master-cycle scheduler
- [ ] Scanline, HBlank, VBlank, and VCount timing
- [ ] Interrupt controller
- [ ] Keypad registers and frontend mapping
- [ ] Bitmap modes 3, 4, and 5
- [ ] First interactive homebrew output

## M4 - DMA, timers, and complete PPU

- [ ] Four DMA channels and trigger timing
- [ ] Four hardware timers and cascading
- [ ] Regular and affine backgrounds
- [ ] Sprites and object attributes
- [ ] Windows, blending, mosaic, and layer priority
- [ ] PPU regression suite

## M5 - Audio and cartridge persistence

- [ ] PSG square, wave, and noise channels
- [ ] Direct Sound FIFOs and DMA integration
- [ ] SDL audio queue and resampling
- [ ] SRAM, Flash, and EEPROM save media
- [ ] Automatic, atomic battery saves

## M6 - Emulator features

- [ ] Versioned save states and quick slots
- [ ] Fast-forward, frame advance, and rewind groundwork
- [ ] Keyboard and gamepad remapping
- [ ] Cheat engine with format-specific parsers
- [ ] ROM folder scanning and richer library metadata
- [ ] Optional color correction and post-processing renderer

## M7 - Compatibility and 1.0

- [ ] Public CPU and timing suites
- [ ] Representative homebrew and commercial-game compatibility matrix
- [ ] Performance profiling and safe optimizations
- [ ] Crash reporting guidance and issue templates
- [ ] Stable save-state policy
- [ ] Signed, reproducible Windows release artifacts

