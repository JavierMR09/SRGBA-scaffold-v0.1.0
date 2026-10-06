#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace srgba::core {

namespace detail {

[[nodiscard]] constexpr std::array<std::uint32_t, 256> make_crc32_table() noexcept {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        auto value = index;
        for (unsigned bit = 0; bit < 8U; ++bit) {
            value = (value & 1U) != 0U ? (value >> 1U) ^ 0xEDB88320U : value >> 1U;
        }
        table[index] = value;
    }
    return table;
}

inline constexpr auto kCrc32Table = make_crc32_table();

} // namespace detail

// CRC-32 (IEEE 802.3), used to identify BIOS images and the ROM a save state belongs to.
[[nodiscard]] inline std::uint32_t crc32(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        value = detail::kCrc32Table[(value ^ byte) & 0xFFU] ^ (value >> 8U);
    }
    return ~value;
}

} // namespace srgba::core
