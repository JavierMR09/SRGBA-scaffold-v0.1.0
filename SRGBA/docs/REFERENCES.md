# Technical References

SRGBA uses documentation and observable hardware tests as implementation references. Reference
material is not copied into release archives unless its redistribution terms explicitly allow it.

## CPU

- ARM7TDMI Data Sheet, ARM DDI 0029E (August 1995)
  - Programmer's model
  - ARM and Thumb instruction encodings and behavior
  - Exceptions and memory interface
- ARM7TDMI Technical Reference Manual, ARM DDI 0210C
  - Pipeline behavior
  - Banked registers and processor modes
  - Exception entry and instruction cycle timing

M1 specifically follows DDI 0210C Chapter 2 for modes, register banking, status registers, and
exception entry, plus DDI 0029E Chapters 4 and 5 for conditions, shifts, ARM data processing, and
Thumb ALU and branch formats.

M2 uses DDI 0029E sections 4.9-4.11 for ARM single, halfword/signed, and block transfers and
sections 5.6-5.15 for Thumb load/store, stack, and multiple-transfer formats. Chapter 6 informs
sequential/non-sequential cycle classification and data-bus width behavior.

## GBA hardware

- [GBATEK](https://problemkaputt.de/gbatek.htm), Martin Korth
  - Memory map, IO registers, timing, DMA, timers, PPU, APU, cartridges, and hardware quirks
  - M2 specifically follows the GBA Memory Map, System Control, GamePak Prefetch, and
    Unpredictable Things sections for region sizes, mirrors, WAITCNT, BIOS protection, open bus,
    and byte writes to video memory
- [Tonc](https://www.coranac.com/tonc/text/), Jasper Vijn
  - Readable explanations of GBA graphics, DMA, timers, interrupts, BIOS calls, and sound

## Emulator features

- [GBATEK cheat device sections](https://problemkaputt.de/gbatek-gba-cheat-codes-gameshark-action-replay-v1-v2.htm)
  (GameShark / Action Replay v1-v2, Pro Action Replay v3, and CodeBreaker): code layouts and the
  TEA-based encryption.
- [EnHacklopedia: Hacking GBA](https://doc.kodewerx.org/hacking_gba.html): code types for
  CodeBreaker and GameShark / Action Replay.
- VisualBoyAdvance-M's cheat notes (a summary of every code type in `gbaCheats.cpp`) were read as
  documentation of code-type behavior; no code was copied.
- The decryption is checked against published master codes: the Pokemon Ruby GameShark v1/v2
  master code decrypts to the "AXVE" game ID, and the Pokemon Emerald Action Replay v3 master
  code decrypts to the "BPEE" game ID.
- The libretro `.cht` cheat-file layout (`cheats`, `cheatN_desc`, `cheatN_code`,
  `cheatN_enable`).

## Accuracy tests

- [jsmolka/gba-tests](https://github.com/jsmolka/gba-tests) (MIT): ARM, Thumb, memory, BIOS, and
  NES (pipeline/DMA) suites. CI downloads a pinned commit and requires every suite to pass; the
  ROMs are never committed to this repository.
- [PeterLemon/GBA](https://github.com/PeterLemon/GBA): bare-metal demos used for manual visual
  checks of tile, affine, Mode 7, sprite, BIOS, and timer behavior during M4.
- [mGBA test suite discussion](https://forums.mgba.io/showthread.php?tid=18)

## Reference priority

When sources disagree, prefer measured hardware-test results, followed by the ARM manuals for CPU
semantics and current GBATEK material for GBA-specific behavior. Document every deliberate
compatibility quirk in code and accompany it with a focused regression test.
