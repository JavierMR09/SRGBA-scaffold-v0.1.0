#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace srgba::app {

// SDL, Dear ImGui and the settings file all use UTF-8, while std::filesystem::path's narrow
// constructor uses the active code page on Windows. These helpers keep non-ASCII paths intact.
[[nodiscard]] inline std::filesystem::path path_from_utf8(const std::string_view text) {
    std::u8string converted;
    converted.reserve(text.size());
    for (const char character : text) {
        converted.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path(converted);
}

[[nodiscard]] inline std::string path_to_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    std::string converted;
    converted.reserve(text.size());
    for (const char8_t character : text) {
        converted.push_back(static_cast<char>(character));
    }
    return converted;
}

} // namespace srgba::app
