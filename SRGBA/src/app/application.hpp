#pragma once

#include "app/input.hpp"
#include "app/library.hpp"
#include "app/settings.hpp"
#include "srgba/core/display_filter.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/rom_library.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

namespace srgba::app {

class Application {
  public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Runs until the window closes. `initial_rom` (for example a file dropped on SRGBA.exe) is
    // loaded at startup.
    [[nodiscard]] int run(const std::optional<std::filesystem::path>& initial_rom = std::nullopt);

  private:
    enum class FileDialogPurpose {
        None,
        Rom,
        Bios,
        RomFolder,
    };

    enum class SettingsTab {
        Video,
        Emulation,
        Controls,
        Audio,
        System,
    };

    enum class CaptureKind {
        None,
        Keyboard,
        Gamepad,
    };

    struct FileDialogResult {
        bool completed{};
        std::optional<std::filesystem::path> selected_path;
        std::string error_message;
    };

    // Developer smoke-test hooks (SRGBA_SCREENSHOT_PATH and friends; see docs/ARCHITECTURE.md).
    struct SmokeTest {
        std::string screenshot_path;
        std::uint64_t capture_after_frames{};
        std::uint64_t presented_frames{};
    };

    [[nodiscard]] bool initialize();
    void shutdown() noexcept;
    void configure_smoke_test();
    void process_events();
    [[nodiscard]] bool process_binding_capture(const SDL_Event& event);
    void handle_key_down(const SDL_KeyboardEvent& key);
    void handle_dropped_file(const std::filesystem::path& path);
    void process_file_dialog_result();
    void update();
    void run_emulation(std::uint64_t now, std::uint64_t elapsed);
    void upload_framebuffer();
    void render();
    void capture_screenshot();

    void draw_menu_bar();
    void draw_state_menu(bool save);
    void draw_workspace();
    void draw_landing_page();
    void draw_library();
    void draw_game_view();
    void draw_speed_overlay(ImVec2 image_min, ImVec2 image_max) const;
    [[nodiscard]] SDL_Texture* display_texture(float display_scale);
    void draw_settings_window();
    void draw_video_settings();
    void draw_emulation_settings();
    void draw_controls_settings();
    void draw_audio_settings();
    void draw_system_settings();
    void draw_binding_cell(std::size_t button, CaptureKind kind, std::size_t slot);
    void draw_cheats_window();
    void draw_about_window();

    void request_open_rom();
    void request_open_bios();
    void request_add_rom_folder();
    void load_rom(const std::filesystem::path& path);
    void load_bios(const std::filesystem::path& path);
    void close_rom();
    void save_state_slot(int slot);
    void load_state_slot(int slot);
    void toggle_pause();
    void advance_frame();
    void reset_game();
    void add_rom_folder(const std::filesystem::path& folder);
    void rescan_library();
    void apply_rewind_settings();
    void apply_bindings(const InputBindings& bindings);
    void select_cheat(int index);
    void persist_cheats();
    void apply_scale_filter() const noexcept;
    void initialize_audio() noexcept;
    void update_audio(bool audible);
    void apply_audio_gain() const noexcept;
    void update_save_status();
    void show_notice(std::string text, bool error = false);
    void save_settings() noexcept;

    static void SDLCALL file_dialog_callback(void* userdata, const char* const* file_list,
                                             int selected_filter);

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* framebuffer_texture_{};
    SDL_Texture* filtered_texture_{};
    int filtered_texture_scale_{};
    std::vector<core::Rgba8> filtered_pixels_;
    core::ColorCorrection color_correction_;
    bool framebuffer_dirty_{true};
    bool display_dirty_{true};

    SDL_AudioStream* audio_stream_{};
    std::vector<std::int16_t> audio_buffer_;
    std::uint64_t observed_saves_written_{};
    core::Emulator emulator_;
    InputMapper input_;
    Settings settings_;
    std::filesystem::path settings_path_;
    RomLibrary library_;
    std::vector<core::RomInfo> library_entries_;
    std::uint64_t library_revision_{};
    std::string library_filter_;
    SmokeTest smoke_test_;

    bool sdl_initialized_{};
    bool imgui_context_created_{};
    bool imgui_platform_initialized_{};
    bool imgui_renderer_initialized_{};
    bool should_quit_{};
    bool show_settings_{};
    bool show_cheats_{};
    bool show_about_{};
    std::optional<SettingsTab> requested_settings_tab_;
    bool file_dialog_open_{};
    FileDialogPurpose file_dialog_purpose_{FileDialogPurpose::None};
    bool status_is_error_{};
    bool fault_reported_{};
    std::string status_message_{"Ready"};
    std::string notice_;
    bool notice_is_error_{};
    std::uint64_t notice_until_ns_{};

    // Timing and speed controls.
    std::uint64_t last_update_ns_{};
    std::uint64_t frame_time_accumulator_ns_{};
    std::uint64_t rewind_accumulator_ns_{};
    std::uint64_t fps_window_start_ns_{};
    std::uint32_t frames_this_window_{};
    double measured_fps_{};
    bool fast_forward_locked_{};
    bool fast_forward_active_{};
    bool rewinding_{};
    bool rewind_exhausted_{};

    // Control remapping: the binding waiting for a key or button press.
    CaptureKind capture_kind_{CaptureKind::None};
    std::size_t capture_button_{};
    std::size_t capture_slot_{};

    // Cheat editor.
    int selected_cheat_{-1};
    std::string cheat_description_;
    std::string cheat_code_;
    int cheat_format_index_{};
    std::string cheat_error_;

    std::mutex file_dialog_mutex_;
    FileDialogResult file_dialog_result_;
};

} // namespace srgba::app
