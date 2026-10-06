#pragma once

#include "srgba/core/display_filter.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace srgba::app {

enum class ScaleFilter {
    Nearest,
    Linear,
};

struct Settings {
    // Video
    int window_width{1280};
    int window_height{800};
    bool integer_scaling{true};
    ScaleFilter scale_filter{ScaleFilter::Nearest};
    bool color_correction{};
    core::LcdFilter lcd_filter{core::LcdFilter::None};
    int filter_strength{50}; // percent

    // System
    bool boot_through_bios{};
    std::string bios_path;

    // Audio
    int audio_volume{80}; // percent
    bool audio_muted{};

    // Emulation
    int fast_forward_speed{4}; // multiple of normal speed; 0 runs as fast as possible
    bool rewind_enabled{true};
    int rewind_buffer_mib{64};

    // Input: GBA button name -> host input names (SDL scancode / gamepad button names). Buttons
    // missing here use the default bindings.
    std::map<std::string, std::vector<std::string>> keyboard_bindings;
    std::map<std::string, std::vector<std::string>> gamepad_bindings;

    // Library
    std::vector<std::string> recent_roms;
    std::vector<std::string> rom_folders;
    bool scan_subfolders{true};

    [[nodiscard]] static Settings load(const std::filesystem::path& path) noexcept;
    void save(const std::filesystem::path& path) const noexcept;
    void add_recent_rom(const std::filesystem::path& path);
};

} // namespace srgba::app
