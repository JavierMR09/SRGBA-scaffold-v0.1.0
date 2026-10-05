#pragma once

#include <cstdint>

namespace srgba::core {

// Bit positions in the IE and IF registers.
enum class Interrupt : std::uint16_t {
    VBlank = 1U << 0U,
    HBlank = 1U << 1U,
    VCount = 1U << 2U,
    Timer0 = 1U << 3U,
    Timer1 = 1U << 4U,
    Timer2 = 1U << 5U,
    Timer3 = 1U << 6U,
    Serial = 1U << 7U,
    Dma0 = 1U << 8U,
    Dma1 = 1U << 9U,
    Dma2 = 1U << 10U,
    Dma3 = 1U << 11U,
    Keypad = 1U << 12U,
    GamePak = 1U << 13U,
};

inline constexpr std::uint16_t kInterruptMask = 0x3FFFU;

// Bit positions in KEYINPUT and KEYCNT. KEYINPUT is active-low on hardware; SRGBA's public API
// uses active-high "pressed" masks built from these values.
enum class Key : std::uint16_t {
    A = 1U << 0U,
    B = 1U << 1U,
    Select = 1U << 2U,
    Start = 1U << 3U,
    Right = 1U << 4U,
    Left = 1U << 5U,
    Up = 1U << 6U,
    Down = 1U << 7U,
    R = 1U << 8U,
    L = 1U << 9U,
};

inline constexpr std::uint16_t kKeyMask = 0x03FFU;

[[nodiscard]] constexpr std::uint16_t operator|(const Key left, const Key right) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint16_t>(left) |
                                      static_cast<std::uint16_t>(right));
}

[[nodiscard]] constexpr std::uint16_t operator|(const std::uint16_t left,
                                                const Key right) noexcept {
    return static_cast<std::uint16_t>(left | static_cast<std::uint16_t>(right));
}

} // namespace srgba::core
