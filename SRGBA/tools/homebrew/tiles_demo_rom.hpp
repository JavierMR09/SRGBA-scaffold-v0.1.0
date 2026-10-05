#pragma once

#include <cstdint>
#include <vector>

namespace srgba::homebrew {

// EWRAM state written by the tiles demo every frame (read by tests).
inline constexpr std::uint32_t kTilesDemoFrameCounterAddress = 0x02000000U;
inline constexpr std::uint32_t kTilesDemoBallXAddress = 0x02000004U;
inline constexpr std::uint32_t kTilesDemoBallYAddress = 0x02000008U;

inline constexpr std::uint32_t kTilesDemoStartX = 112;
inline constexpr std::uint32_t kTilesDemoStartY = 72;
inline constexpr std::uint32_t kTilesDemoShadowOffset = 6;
// Square channel 1 frequency register value for the A-button chirp (~293 Hz, sweeping upward).
inline constexpr std::uint32_t kTilesDemoChirpFrequency = 1600;

// BGR555 colors used by the demo.
inline constexpr std::uint16_t kTilesDemoLightTile = 0x5186U;
inline constexpr std::uint16_t kTilesDemoDarkTile = 0x3904U;
inline constexpr std::uint16_t kTilesDemoBallOutline = 0x08D4U;
inline constexpr std::uint16_t kTilesDemoBallFill = 0x121FU;
inline constexpr std::uint16_t kTilesDemoBallHighlight = 0x539FU;

// Builds "SRGBA Tiles Demo": an original ARM program that shows the M4 hardware working
// together. Palettes and graphics are loaded with DMA, BG0 is a scrolling tile-mode checkerboard,
// the D-pad moves a 16x16 ball sprite with a semi-transparent shadow, and a highlight band
// (window 0 + brightness effect) slides across the screen driven by hardware timer 2. Press A
// for a rising chirp (square channel with sweep) and B for a noise burst.
[[nodiscard]] std::vector<std::uint8_t> build_tiles_demo_rom();

// Palette index (0 = transparent, 1 = outline, 2 = fill, 3 = highlight) of the ball at (x, y).
[[nodiscard]] std::uint8_t tiles_demo_ball_pixel(unsigned x, unsigned y) noexcept;

} // namespace srgba::homebrew
