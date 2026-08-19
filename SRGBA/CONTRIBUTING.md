# Contributing to SRGBA

SRGBA is in early development. Small, test-backed changes are strongly preferred over broad
untested implementations.

## Before submitting a change

1. Keep platform APIs out of `include/srgba/core` and `src/core`.
2. Add or update a focused test for behavior changed in the core.
3. Run the configured build and test presets.
4. Format C++ files with the repository `.clang-format` settings.
5. Do not add ROMs, BIOS dumps, proprietary SDK files, or copied emulator implementations.

## Commit scope

Use commits that each leave the project building. For instruction implementations, prefer one
coherent instruction family plus tests per change. Accuracy fixes should cite the relevant manual
section or hardware test in the commit or pull-request description.

