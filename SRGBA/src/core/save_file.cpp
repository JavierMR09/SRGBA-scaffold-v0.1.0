#include "srgba/core/save_file.hpp"

#include <fstream>
#include <system_error>

namespace srgba::core {

std::optional<std::vector<std::uint8_t>> read_binary_file(const std::filesystem::path& path,
                                                          const std::size_t size_limit) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > size_limit) {
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        return std::nullopt;
    }
    return bytes;
}

bool write_file_atomically(const std::filesystem::path& path,
                           const std::span<const std::uint8_t> bytes,
                           std::string& error_message) noexcept {
    try {
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                error_message = "Could not create " + temporary.string();
                return false;
            }
            output.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) {
                error_message = "Could not write " + temporary.string();
                return false;
            }
        }
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            error_message = "Could not replace " + path.string();
            return false;
        }
        error_message.clear();
        return true;
    } catch (...) {
        error_message = "Could not write the save file.";
        return false;
    }
}

} // namespace srgba::core
