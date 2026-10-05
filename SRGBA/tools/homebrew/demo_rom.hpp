#pragma once

#include <cstdint>
#include <vector>

namespace srgba::homebrew {

// EWRAM word incremented by the demo's VBlank interrupt handler.
inline constexpr std::uint32_t kDemoFrameCounterAddress = 0x02000000U;
// EWRAM words holding the square's current position (updated once per frame).
inline constexpr std::uint32_t kDemoPlayerXAddress = 0x02000004U;
inline constexpr std::uint32_t kDemoPlayerYAddress = 0x02000008U;

inline constexpr std::uint32_t kDemoStartX = 112;
inline constexpr std::uint32_t kDemoStartY = 72;
inline constexpr std::uint32_t kDemoSquareSize = 16;
inline constexpr std::uint16_t kDemoSquareColor = 0x03FFU;          // yellow
inline constexpr std::uint16_t kDemoSquareAlternateColor = 0x7C1FU; // magenta while A is held

// Builds "SRGBA Demo", an original ARM homebrew program used by tests and shipped with releases:
// it draws a gradient in bitmap mode 3, waits for VBlank through the BIOS, and moves a square
// with the D-pad. Press A to change its color.
[[nodiscard]] std::vector<std::uint8_t> build_demo_rom();

// Returns the background color the demo draws at (x, y), in BGR555.
[[nodiscard]] constexpr std::uint16_t demo_background_color(const std::uint32_t x,
                                                            const std::uint32_t y) noexcept {
    return static_cast<std::uint16_t>((((y * 205U) >> 10U) << 10U) | ((x >> 3U) << 5U) | 4U);
}

// Fills in a valid cartridge header (title, game code, fixed value, and checksum).
void write_cartridge_header(std::vector<std::uint8_t>& rom, const char* title,
                            const char* game_code);

} // namespace srgba::homebrew
