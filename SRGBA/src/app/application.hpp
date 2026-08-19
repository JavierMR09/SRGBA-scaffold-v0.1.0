#pragma once

#include "app/settings.hpp"
#include "srgba/core/emulator.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

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

    [[nodiscard]] int run();

  private:
    enum class FileDialogPurpose {
        None,
        Rom,
        Bios,
    };

    struct FileDialogResult {
        bool completed{};
        std::optional<std::filesystem::path> selected_path;
        std::string error_message;
    };

    [[nodiscard]] bool initialize();
    void shutdown() noexcept;
    void process_events();
    void process_file_dialog_result();
    void update();
    void render();

    void draw_menu_bar();
    void draw_workspace();
    void draw_landing_page();
    void draw_game_view();
    void draw_settings_window();
    void draw_about_window();

    void request_open_rom();
    void request_open_bios();
    void load_rom(const std::filesystem::path& path);
    void load_bios(const std::filesystem::path& path);
    void close_rom();
    void apply_scale_filter() const noexcept;
    void save_settings() noexcept;

    static void SDLCALL file_dialog_callback(void* userdata, const char* const* file_list,
                                             int selected_filter);

    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* framebuffer_texture_{};
    core::Emulator emulator_;
    Settings settings_;
    std::filesystem::path settings_path_;

    bool sdl_initialized_{};
    bool imgui_context_created_{};
    bool imgui_platform_initialized_{};
    bool imgui_renderer_initialized_{};
    bool should_quit_{};
    bool show_settings_{};
    bool show_about_{};
    bool file_dialog_open_{};
    FileDialogPurpose file_dialog_purpose_{FileDialogPurpose::None};
    bool status_is_error_{};
    std::string status_message_{"Ready"};

    std::mutex file_dialog_mutex_;
    FileDialogResult file_dialog_result_;
};

} // namespace srgba::app
