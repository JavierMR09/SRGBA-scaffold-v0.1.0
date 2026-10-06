#pragma once

#include "srgba/core/framebuffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace srgba::core {

class GbaBus;
class Scheduler;
class StateReader;
class StateWriter;

// Picture processing unit: display timing (HBlank, VBlank, VCount and their interrupts and DMA
// triggers) and a scanline renderer covering every video mode, regular and affine backgrounds,
// sprites, windows, color special effects, and mosaic.
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
    // Save-state serialization (see state_io.hpp).
    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

    // Scheduler event handlers. `timestamp` is the event's exact deadline.
    void on_hblank_start(GbaBus& bus, Scheduler& scheduler, std::uint64_t timestamp,
                         Framebuffer& framebuffer) noexcept;
    void on_scanline_end(GbaBus& bus, Scheduler& scheduler, std::uint64_t timestamp) noexcept;

    [[nodiscard]] std::uint32_t vcount() const noexcept;

    // Renders one visible scanline from the current register, VRAM, palette, and OAM state, then
    // advances the affine reference points. Lines must be rendered in order within a frame.
    void render_scanline(GbaBus& bus, std::uint32_t line, Framebuffer& framebuffer) noexcept;
    // Renders lines 0-159 in order. Used by tests and tools that work without display timing.
    void render_frame(GbaBus& bus, Framebuffer& framebuffer) noexcept;

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
    // Layer line buffers hold BGR555 colors; this bit marks a transparent pixel.
    static constexpr std::uint16_t kTransparent = 0x8000U;

    struct ObjectPixel {
        std::uint16_t color{kTransparent};
        std::uint8_t priority{4};
        bool semi_transparent{};
    };

    struct AffineReference {
        std::int32_t x{};
        std::int32_t y{};
    };

    void update_status(GbaBus& bus) noexcept;
    void render_text_background(const GbaBus& bus, std::size_t background,
                                std::uint32_t line) noexcept;
    void render_affine_background(const GbaBus& bus, std::size_t background) noexcept;
    void render_bitmap_background(const GbaBus& bus, std::uint16_t control) noexcept;
    void render_objects(const GbaBus& bus, std::uint16_t control, std::uint32_t line) noexcept;
    void compute_windows(const GbaBus& bus, std::uint16_t control, std::uint32_t line) noexcept;
    void compose(const GbaBus& bus, std::uint16_t control, std::uint32_t line,
                 Framebuffer& framebuffer) noexcept;
    void reload_affine(const GbaBus& bus, std::size_t index) noexcept;

    std::uint32_t vcount_{};
    bool hblank_{};
    std::array<AffineReference, 2> affine_{};
    std::array<std::array<std::uint16_t, 240>, 4> background_lines_{};
    std::array<bool, 4> background_active_{};
    std::array<ObjectPixel, 240> object_line_{};
    std::array<bool, 240> object_window_{};
    std::array<std::uint8_t, 240> window_mask_{};
};

} // namespace srgba::core
