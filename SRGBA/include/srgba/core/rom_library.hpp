#pragma once

#include "srgba/core/backup.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace srgba::core {

// What the game library shows for one ROM file.
struct RomInfo {
    std::filesystem::path path;
    std::string title; // header title, or the file name when the header has none
    std::string game_code;
    std::string maker_code;
    std::uint8_t version{};
    std::uintmax_t size{};
    SaveType save_type{SaveType::None};
    bool header_valid{};
};

// Reads a ROM's header and detects its save chip (which needs the whole file). Returns nullopt
// and explains why when the file cannot be read or is not a GBA ROM.
[[nodiscard]] std::optional<RomInfo> read_rom_info(const std::filesystem::path& path,
                                                   std::string& error_message);

// True for the file extensions SRGBA treats as GBA ROMs (.gba and .agb, any case).
[[nodiscard]] bool has_rom_extension(const std::filesystem::path& path);

// Lists ROM files in `folder` (and its subfolders when `recursive`), sorted by path. Folders
// that cannot be read are skipped; at most `limit` files are returned.
[[nodiscard]] std::vector<std::filesystem::path>
find_rom_files(const std::filesystem::path& folder, bool recursive, std::size_t limit = 5000);

} // namespace srgba::core
