# SRGBA Architecture

## Design goals

1. Keep emulated hardware deterministic and independent of host APIs.
2. Model timing from the first CPU implementation instead of assuming one cycle per instruction.
3. Make every hardware block testable without opening a window.
4. Serialize logical state explicitly; never save raw C++ object memory.
5. Keep the first implementation understandable before pursuing JIT compilation or heavy
   optimization.

## Runtime boundary

The desktop frontend owns SDL, Dear ImGui, host input, audio devices, file dialogs, frame pacing,
and presentation. The core owns the cartridge, ARM7TDMI state, GBA bus, scheduling, PPU, APU, DMA,
timers, keypad registers, interrupts, and future save-state serialization.

The intended frontend/core exchange is deliberately small:

- Load or unload cartridge and BIOS bytes
- Reset or advance the machine
- Set GBA key states
- Read a completed 240x160 RGBA framebuffer
- Drain generated stereo audio samples
- Serialize or deserialize versioned machine state

No core header may include SDL or Dear ImGui headers.

## Planned timing model

The GBA master clock is 2^24 Hz. A central scheduler will track the next event for the PPU, DMA,
timers, audio FIFOs, and interrupts. Each ARM or Thumb instruction will perform bus accesses and
return the cycles consumed, including sequential/non-sequential access timing and pipeline refill
costs. The scheduler then advances hardware to the new master timestamp.

The first implementation will be an interpreter. A decoded-instruction cache or JIT can be
considered only after the interpreter passes instruction and timing tests.

## Planned core modules

```text
Emulator
├── Scheduler
├── Arm7Tdmi
│   ├── ARM decoder/executor
│   ├── Thumb decoder/executor
│   ├── banked registers
│   └── exceptions and pipeline
├── Bus
│   ├── BIOS, EWRAM, IWRAM
│   ├── IO registers
│   ├── palette RAM, VRAM, OAM
│   └── Game Pak ROM and backup media
├── PPU
├── APU
├── DMA
├── Timers
├── Interrupt controller
└── Keypad
```

## Persistence rules

- Battery saves will be keyed by ROM identity and written through a temporary file.
- Save states will start with a magic value, schema version, ROM hash, and component sections.
- Unsupported future state versions must fail cleanly rather than partially loading.
- Frontend settings are not part of an emulated save state.

## Testing strategy

- Unit tests: decoding, barrel shifter, ALU flags, bank switching, memory mirroring, register masks
- Instruction vectors: ARM and Thumb result/flag comparisons
- Hardware test ROMs: public and redistributable suites only
- PPU regression tests: deterministic framebuffer hashes and selected reference images
- Integration tests: homebrew boot, input, DMA, timers, interrupts, and cartridge saves
- Sanitizer jobs: core-only builds where supported

## Clean-room rule

Specifications, public research, and test behavior may guide implementation. Do not paste or
translate implementation code from another emulator. Code from GPL emulators is not compatible
with SRGBA's MIT license unless the project's licensing strategy is deliberately changed first.

