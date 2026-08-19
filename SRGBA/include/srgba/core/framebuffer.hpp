#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace srgba::core {

inline constexpr std::size_t kScreenWidth = 240;
inline constexpr std::size_t kScreenHeight = 160;
inline constexpr std::size_t kFramebufferPixelCount = kScreenWidth * kScreenHeight;

struct Rgba8 {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};

    friend constexpr bool operator==(const Rgba8&, const Rgba8&) = default;
};

static_assert(sizeof(Rgba8) == 4, "The SDL texture upload expects tightly packed RGBA pixels");

using Framebuffer = std::array<Rgba8, kFramebufferPixelCount>;

} // namespace srgba::core
