#include "srgba/core/rom_library.hpp"

#include "srgba/core/cartridge.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <system_error>

namespace srgba::core {

std::optional<RomInfo> read_rom_info(const std::filesystem::path& path,
                                     std::string& error_message) {
    try {
        const auto cartridge = Cartridge::load(path);
        const auto& header = cartridge.header();
        RomInfo info;
        info.path = path;
        info.title = header.title.empty() ? path.stem().string() : header.title;
        info.game_code = header.game_code;
        info.maker_code = header.maker_code;
        info.version = header.software_version;
        info.size = cartridge.size();
        info.save_type = detect_save_type(cartridge.bytes());
        info.header_valid = header.is_valid();
        error_message.clear();
        return info;
    } catch (const std::exception& exception) {
        error_message = exception.what();
        return std::nullopt;
    }
}

bool has_rom_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return extension == ".gba" || extension == ".agb";
}

std::vector<std::filesystem::path> find_rom_files(const std::filesystem::path& folder,
                                                  const bool recursive, const std::size_t limit) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    const auto consider = [&](const std::filesystem::directory_entry& entry) {
        std::error_code status_error;
        if (entry.is_regular_file(status_error) && has_rom_extension(entry.path())) {
            files.push_back(entry.path());
        }
    };

    if (recursive) {
        constexpr auto options = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator iterator(folder, options, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end && files.size() < limit) {
            consider(*iterator);
            iterator.increment(error);
        }
    } else {
        std::filesystem::directory_iterator iterator(folder, error);
        const std::filesystem::directory_iterator end;
        while (!error && iterator != end && files.size() < limit) {
            consider(*iterator);
            iterator.increment(error);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace srgba::core
