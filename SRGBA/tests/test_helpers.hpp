#pragma once

#include "srgba/core/cartridge.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace srgba::tests {

inline void write_ascii(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                        const std::size_t length, const std::string_view text) {
    const auto count = std::min(length, text.size());
    for (std::size_t index = 0; index < count; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(text[index]);
    }
}

inline void write_word(std::vector<std::uint8_t>& bytes, const std::size_t offset,
                       const std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

[[nodiscard]] inline std::vector<std::uint8_t> make_valid_test_rom() {
    std::vector<std::uint8_t> bytes(256, 0);
    write_ascii(bytes, 0xA0, 12, "SRGBA TEST");
    write_ascii(bytes, 0xAC, 4, "SRGE");
    write_ascii(bytes, 0xB0, 2, "01");
    bytes[0xB2] = 0x96;
    bytes[0xB3] = 0;
    bytes[0xBC] = 1;

    std::uint8_t checksum = 0;
    for (std::size_t offset = 0xA0; offset <= 0xBC; ++offset) {
        checksum = static_cast<std::uint8_t>(checksum - bytes[offset]);
    }
    bytes[0xBD] = static_cast<std::uint8_t>(checksum - 0x19U);
    return bytes;
}

[[nodiscard]] inline std::vector<std::uint8_t> make_m2_cpu_test_rom() {
    auto bytes = make_valid_test_rom();
    constexpr std::array<std::uint32_t, 7> instructions{
        0xE3A00402U, // MOV r0, #0x02000000
        0xE3A0102AU, // MOV r1, #42
        0xE5801000U, // STR r1, [r0]
        0xE5902000U, // LDR r2, [r0]
        0xE2822001U, // ADD r2, r2, #1
        0xE5802004U, // STR r2, [r0, #4]
        0xEAFFFFFEU, // B .
    };
    for (std::size_t index = 0; index < instructions.size(); ++index) {
        write_word(bytes, index * sizeof(std::uint32_t), instructions[index]);
    }
    return bytes;
}

class TemporaryRom {
  public:
    explicit TemporaryRom(const std::vector<std::uint8_t>& bytes) {
        const auto unique_value =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("srgba-test-" + std::to_string(unique_value) + ".gba");

        std::ofstream output(path_, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            throw std::runtime_error("Could not create a temporary test ROM.");
        }
    }

    ~TemporaryRom() {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    TemporaryRom(const TemporaryRom&) = delete;
    TemporaryRom& operator=(const TemporaryRom&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

} // namespace srgba::tests
