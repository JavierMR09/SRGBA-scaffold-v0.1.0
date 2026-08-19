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

## GBA hardware

- [GBATEK](https://problemkaputt.de/gbatek.htm), Martin Korth
  - Memory map, IO registers, timing, DMA, timers, PPU, APU, cartridges, and hardware quirks
- [Tonc](https://www.coranac.com/tonc/text/), Jasper Vijn
  - Readable explanations of GBA graphics, DMA, timers, interrupts, BIOS calls, and sound

## Accuracy tests

- [mGBA test suite discussion](https://forums.mgba.io/showthread.php?tid=18)
- Additional public ARM and GBA test suites will be reviewed for licensing before inclusion.

## Reference priority

When sources disagree, prefer measured hardware-test results, followed by the ARM manuals for CPU
semantics and current GBATEK material for GBA-specific behavior. Document every deliberate
compatibility quirk in code and accompany it with a focused regression test.

