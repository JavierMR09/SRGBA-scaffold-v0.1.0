#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace srgba::core {

inline constexpr std::size_t kGbaHeaderSize = 0xC0;
inline constexpr std::size_t kMaximumRomSize = 32U * 1024U * 1024U;

struct RomHeader {
    std::string title;
    std::string game_code;
    std::string maker_code;
    std::uint8_t unit_code{};
    std::uint8_t software_version{};
    std::uint8_t stored_checksum{};
    std::uint8_t calculated_checksum{};
    bool fixed_value_valid{};
    bool checksum_valid{};

    [[nodiscard]] bool is_valid() const noexcept {
        return fixed_value_valid && checksum_valid;
    }
};

[[nodiscard]] RomHeader parse_rom_header(std::span<const std::uint8_t> bytes);

class Cartridge {
  public:
    [[nodiscard]] static Cartridge load(const std::filesystem::path& path);

    [[nodiscard]] const RomHeader& header() const noexcept;
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

  private:
    Cartridge(std::filesystem::path path, std::vector<std::uint8_t> bytes, RomHeader header);

    std::filesystem::path path_;
    std::vector<std::uint8_t> bytes_;
    RomHeader header_;
};

} // namespace srgba::core
