#pragma once

#include "srgba/core/cartridge.hpp"

#include <algorithm>
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
