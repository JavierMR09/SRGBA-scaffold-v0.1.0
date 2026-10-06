#include "app/settings.hpp"

#include "app/paths.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace srgba::app {
namespace {

constexpr std::size_t kMaximumRecentRoms = 10;
constexpr std::size_t kMaximumRomFolders = 32;

[[nodiscard]] std::string scale_filter_name(const ScaleFilter filter) {
    return filter == ScaleFilter::Linear ? "linear" : "nearest";
}

[[nodiscard]] ScaleFilter parse_scale_filter(const std::string& name) {
    return name == "linear" ? ScaleFilter::Linear : ScaleFilter::Nearest;
}

[[nodiscard]] std::string lcd_filter_name(const core::LcdFilter filter) {
    switch (filter) {
    case core::LcdFilter::Grid:
        return "grid";
    case core::LcdFilter::Scanlines:
        return "scanlines";
    case core::LcdFilter::None:
        break;
    }
    return "none";
}

[[nodiscard]] core::LcdFilter parse_lcd_filter(const std::string& name) {
    if (name == "grid") {
        return core::LcdFilter::Grid;
    }
    return name == "scanlines" ? core::LcdFilter::Scanlines : core::LcdFilter::None;
}

void read_strings(const nlohmann::json& json, const char* key, const std::size_t limit,
                  std::vector<std::string>& destination) {
    if (!json.contains(key) || !json[key].is_array()) {
        return;
    }
    for (const auto& item : json[key]) {
        if (item.is_string() && destination.size() < limit) {
            destination.push_back(item.get<std::string>());
        }
    }
}

void read_bindings(const nlohmann::json& json, const char* key,
                   std::map<std::string, std::vector<std::string>>& destination) {
    if (!json.contains(key) || !json[key].is_object()) {
        return;
    }
    for (const auto& [button, inputs] : json[key].items()) {
        if (!inputs.is_array()) {
            continue;
        }
        auto& names = destination[button];
        for (const auto& input : inputs) {
            if (input.is_string() && names.size() < 4U) {
                names.push_back(input.get<std::string>());
            }
        }
    }
}

} // namespace

Settings Settings::load(const std::filesystem::path& path) noexcept {
    Settings settings;
    try {
        std::ifstream input(path);
        if (!input) {
            return settings;
        }

        const auto json = nlohmann::json::parse(input);
        settings.window_width = std::clamp(json.value("window_width", 1280), 640, 7680);
        settings.window_height = std::clamp(json.value("window_height", 800), 480, 4320);
        settings.integer_scaling = json.value("integer_scaling", true);
        settings.scale_filter = parse_scale_filter(json.value("scale_filter", "nearest"));
        settings.color_correction = json.value("color_correction", false);
        settings.lcd_filter = parse_lcd_filter(json.value("lcd_filter", "none"));
        settings.filter_strength = std::clamp(json.value("filter_strength", 50), 0, 100);
        settings.boot_through_bios = json.value("boot_through_bios", false);
        settings.bios_path = json.value("bios_path", std::string{});
        settings.audio_volume = std::clamp(json.value("audio_volume", 80), 0, 100);
        settings.audio_muted = json.value("audio_muted", false);
        settings.fast_forward_speed = std::clamp(json.value("fast_forward_speed", 4), 0, 16);
        if (settings.fast_forward_speed == 1) {
            settings.fast_forward_speed = 2;
        }
        settings.rewind_enabled = json.value("rewind_enabled", true);
        settings.rewind_buffer_mib = std::clamp(json.value("rewind_buffer_mib", 64), 16, 1024);
        read_bindings(json, "keyboard_bindings", settings.keyboard_bindings);
        read_bindings(json, "gamepad_bindings", settings.gamepad_bindings);
        read_strings(json, "recent_roms", kMaximumRecentRoms, settings.recent_roms);
        read_strings(json, "rom_folders", kMaximumRomFolders, settings.rom_folders);
        settings.scan_subfolders = json.value("scan_subfolders", true);
    } catch (...) {
        return Settings{};
    }
    return settings;
}

void Settings::save(const std::filesystem::path& path) const noexcept {
    try {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);

        const nlohmann::json json{
            {"window_width", window_width},
            {"window_height", window_height},
            {"integer_scaling", integer_scaling},
            {"scale_filter", scale_filter_name(scale_filter)},
            {"color_correction", color_correction},
            {"lcd_filter", lcd_filter_name(lcd_filter)},
            {"filter_strength", filter_strength},
            {"boot_through_bios", boot_through_bios},
            {"bios_path", bios_path},
            {"audio_volume", audio_volume},
            {"audio_muted", audio_muted},
            {"fast_forward_speed", fast_forward_speed},
            {"rewind_enabled", rewind_enabled},
            {"rewind_buffer_mib", rewind_buffer_mib},
            {"keyboard_bindings", keyboard_bindings},
            {"gamepad_bindings", gamepad_bindings},
            {"recent_roms", recent_roms},
            {"rom_folders", rom_folders},
            {"scan_subfolders", scan_subfolders},
        };

        std::ofstream output(path, std::ios::trunc);
        if (output) {
            output << json.dump(2) << '\n';
        }
    } catch (...) {
        // Settings persistence must never stop the emulator from shutting down.
    }
}

void Settings::add_recent_rom(const std::filesystem::path& path) {
    const auto value = path_to_utf8(path.lexically_normal());
    recent_roms.erase(std::remove(recent_roms.begin(), recent_roms.end(), value),
                      recent_roms.end());
    recent_roms.insert(recent_roms.begin(), value);
    if (recent_roms.size() > kMaximumRecentRoms) {
        recent_roms.resize(kMaximumRecentRoms);
    }
}

} // namespace srgba::app
