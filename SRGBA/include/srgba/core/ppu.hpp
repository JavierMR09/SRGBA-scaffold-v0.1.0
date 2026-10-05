#pragma once

#include "srgba/core/framebuffer.hpp"

#include <cstdint>

namespace srgba::core {

class GbaBus;
class Scheduler;

// Picture processing unit. M3 implements display timing (HBlank, VBlank, VCount, and their
// interrupts) and the three bitmap modes. Tile modes 0-2, sprites, and effects arrive in M4.
class Ppu {
  public:
    static constexpr std::uint32_t kCyclesPerDot = 4;
    static constexpr std::uint32_t kVisibleDots = 240;
    static constexpr std::uint32_t kHDrawCycles = 1006; // HBlank flag rises 46 cycles after dot 240
    static constexpr std::uint32_t kCyclesPerLine = 1232;
    static constexpr std::uint32_t kVisibleLines = 160;
    static constexpr std::uint32_t kLinesPerFrame = 228;
    static constexpr std::uint32_t kCyclesPerFrame = kCyclesPerLine * kLinesPerFrame;

    void reset(GbaBus& bus, Scheduler& scheduler) noexcept;

    // Scheduler event handlers. `timestamp` is the event's exact deadline.
    void on_hblank_start(GbaBus& bus, Scheduler& scheduler, std::uint64_t timestamp,
                         Framebuffer& framebuffer) noexcept;
    void on_scanline_end(GbaBus& bus, Scheduler& scheduler, std::uint64_t timestamp) noexcept;

    [[nodiscard]] std::uint32_t vcount() const noexcept;

    // Renders one visible scanline from the current register and VRAM state.
    static void render_scanline(const GbaBus& bus, std::uint32_t line,
                                Framebuffer& framebuffer) noexcept;

    [[nodiscard]] static constexpr Rgba8 to_rgba(const std::uint16_t color) noexcept {
        const auto expand = [](const std::uint16_t channel) {
            return static_cast<std::uint8_t>((channel << 3U) | (channel >> 2U));
        };
        return Rgba8{
            expand(static_cast<std::uint16_t>(color & 0x1FU)),
            expand(static_cast<std::uint16_t>((color >> 5U) & 0x1FU)),
            expand(static_cast<std::uint16_t>((color >> 10U) & 0x1FU)),
            255,
        };
    }

  private:
    void update_status(GbaBus& bus) noexcept;

    std::uint32_t vcount_{};
    bool hblank_{};
};

} // namespace srgba::core
