#include "srgba/core/display_filter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace srgba::core {
namespace {

// The LCD's response is far steeper than a PC monitor's 2.2 gamma.
constexpr double kLcdGamma = 3.2;
constexpr double kDisplayGamma = 2.2;
// How much each emitted primary mixes in the others (rows: output red, green, blue).
constexpr std::array<std::array<double, 3>, 3> kMix{{
    {0.80, 0.16, 0.04},
    {0.10, 0.80, 0.10},
    {0.08, 0.16, 0.76},
}};

[[nodiscard]] std::uint8_t to_byte(const double linear) noexcept {
    const auto encoded = std::pow(std::clamp(linear, 0.0, 1.0), 1.0 / kDisplayGamma);
    return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
}

[[nodiscard]] std::size_t bgr555_index(const Rgba8 color) noexcept {
    // The PPU expands 5-bit channels as (c << 3) | (c >> 2), so the top five bits recover them.
    return static_cast<std::size_t>(color.red >> 3U) |
           (static_cast<std::size_t>(color.green >> 3U) << 5U) |
           (static_cast<std::size_t>(color.blue >> 3U) << 10U);
}

[[nodiscard]] std::uint8_t scale_channel(const std::uint8_t channel,
                                         const std::uint32_t factor) noexcept {
    return static_cast<std::uint8_t>((channel * factor) >> 8U);
}

} // namespace

ColorCorrection::ColorCorrection() : table_(32768U) {
    std::array<double, 32> linear{};
    for (std::size_t level = 0; level < linear.size(); ++level) {
        linear[level] = std::pow(static_cast<double>(level) / 31.0, kLcdGamma);
    }
    for (std::size_t index = 0; index < table_.size(); ++index) {
        const std::array<double, 3> source{linear[index & 0x1FU], linear[(index >> 5U) & 0x1FU],
                                           linear[(index >> 10U) & 0x1FU]};
        std::array<std::uint8_t, 3> output{};
        for (std::size_t channel = 0; channel < 3U; ++channel) {
            const auto& weights = kMix[channel];
            output[channel] =
                to_byte(weights[0] * source[0] + weights[1] * source[1] + weights[2] * source[2]);
        }
        table_[index] = Rgba8{output[0], output[1], output[2], 255};
    }
}

Rgba8 ColorCorrection::apply(const Rgba8 color) const noexcept {
    return table_[bgr555_index(color)];
}

void render_display(const Framebuffer& source, const ColorCorrection* correction,
                    const LcdFilter filter, const int scale, const float strength,
                    std::vector<Rgba8>& destination) {
    const auto factor = static_cast<std::size_t>(std::clamp(scale, 1, kMaximumDisplayScale));
    const auto width = kScreenWidth * factor;
    destination.resize(width * kScreenHeight * factor);

    // Brightness (out of 256) for each position inside a scaled pixel.
    const auto amount = std::clamp(strength, 0.0F, 1.0F);
    const auto darken = [&](const float depth) {
        return static_cast<std::uint32_t>(std::lround(256.0F * (1.0F - depth * amount)));
    };
    std::array<std::array<std::uint32_t, kMaximumDisplayScale>, kMaximumDisplayScale> mask{};
    for (std::size_t row = 0; row < factor; ++row) {
        for (std::size_t column = 0; column < factor; ++column) {
            std::uint32_t value = 256;
            if (factor >= 2U && filter == LcdFilter::Grid) {
                const bool edge_row = row == factor - 1U;
                const bool edge_column = column == factor - 1U;
                if (edge_row && edge_column) {
                    value = darken(0.7F);
                } else if (edge_row || edge_column) {
                    value = darken(0.45F);
                }
            } else if (factor >= 2U && filter == LcdFilter::Scanlines) {
                const auto dark_rows = std::max<std::size_t>(1U, factor / 3U);
                if (row >= factor - dark_rows) {
                    value = darken(0.6F);
                }
            }
            mask[row][column] = value;
        }
    }

    std::array<Rgba8, kScreenWidth> line{};
    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            const auto color = source[y * kScreenWidth + x];
            line[x] = correction != nullptr ? correction->apply(color) : color;
        }
        for (std::size_t row = 0; row < factor; ++row) {
            auto* output = &destination[(y * factor + row) * width];
            const auto& weights = mask[row];
            for (std::size_t x = 0; x < kScreenWidth; ++x) {
                const auto color = line[x];
                for (std::size_t column = 0; column < factor; ++column) {
                    const auto weight = weights[column];
                    *output++ = weight == 256U ? color
                                               : Rgba8{scale_channel(color.red, weight),
                                                       scale_channel(color.green, weight),
                                                       scale_channel(color.blue, weight), 255};
                }
            }
        }
    }
}

} // namespace srgba::core
