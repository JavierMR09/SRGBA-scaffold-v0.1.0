#pragma once

#include "srgba/core/framebuffer.hpp"

#include <cstdint>
#include <vector>

namespace srgba::core {

// Optional looks for the 240x160 picture on a PC monitor.
enum class LcdFilter : std::uint8_t {
    None,
    Grid,      // dark lines between pixels, like the GBA's LCD up close
    Scanlines, // dark horizontal lines
};

inline constexpr int kMaximumDisplayScale = 8;

// Approximates the colors of the original GBA's unlit reflective LCD: much darker midtones and
// primaries that bleed into one another. Games were tuned for that screen, so their bright,
// saturated palettes look closer to the intended art after correction.
class ColorCorrection {
  public:
    ColorCorrection();

    [[nodiscard]] Rgba8 apply(Rgba8 color) const noexcept;

  private:
    std::vector<Rgba8> table_; // indexed by the 15-bit BGR555 color
};

// Produces the displayed image: color correction (when `correction` is set), then an integer
// upscale by `scale` (1-8) with the filter's mask darkening pixel edges by `strength` (0-1).
// `destination` is resized to (240 * scale) x (160 * scale) pixels.
void render_display(const Framebuffer& source, const ColorCorrection* correction, LcdFilter filter,
                    int scale, float strength, std::vector<Rgba8>& destination);

} // namespace srgba::core
