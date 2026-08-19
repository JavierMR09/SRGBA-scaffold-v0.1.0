#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace srgba::app {

enum class ScaleFilter {
    Nearest,
    Linear,
};

struct Settings {
    int window_width{1280};
    int window_height{800};
    bool integer_scaling{true};
    ScaleFilter scale_filter{ScaleFilter::Nearest};
    bool boot_through_bios{};
    std::string bios_path;
    std::vector<std::string> recent_roms;

    [[nodiscard]] static Settings load(const std::filesystem::path& path) noexcept;
    void save(const std::filesystem::path& path) const noexcept;
    void add_recent_rom(const std::filesystem::path& path);
};

} // namespace srgba::app
