#include "app/settings.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace srgba::app {
namespace {

constexpr std::size_t kMaximumRecentRoms = 10;

[[nodiscard]] std::string scale_filter_name(const ScaleFilter filter) {
    return filter == ScaleFilter::Linear ? "linear" : "nearest";
}

[[nodiscard]] ScaleFilter parse_scale_filter(const std::string& name) {
    return name == "linear" ? ScaleFilter::Linear : ScaleFilter::Nearest;
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
        settings.boot_through_bios = json.value("boot_through_bios", false);
        settings.bios_path = json.value("bios_path", std::string{});

        if (json.contains("recent_roms") && json["recent_roms"].is_array()) {
            for (const auto& item : json["recent_roms"]) {
                if (item.is_string() && settings.recent_roms.size() < kMaximumRecentRoms) {
                    settings.recent_roms.push_back(item.get<std::string>());
                }
            }
        }
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
            {"boot_through_bios", boot_through_bios},
            {"bios_path", bios_path},
            {"recent_roms", recent_roms},
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
    const auto value = path.lexically_normal().string();
    recent_roms.erase(std::remove(recent_roms.begin(), recent_roms.end(), value),
                      recent_roms.end());
    recent_roms.insert(recent_roms.begin(), value);
    if (recent_roms.size() > kMaximumRecentRoms) {
        recent_roms.resize(kMaximumRecentRoms);
    }
}

} // namespace srgba::app
