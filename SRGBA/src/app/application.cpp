#include "app/application.hpp"

#include "srgba/core/framebuffer.hpp"

#include <SDL3/SDL_dialog.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <utility>

namespace srgba::app {
namespace {

constexpr const char* kWindowTitle = "SRGBA";
constexpr SDL_DialogFileFilter kRomFilters[] = {
    {"Game Boy Advance ROMs", "gba;agb"},
    {"All files", "*"},
};
constexpr SDL_DialogFileFilter kBiosFilters[] = {
    {"Game Boy Advance BIOS images", "bin;rom"},
    {"All files", "*"},
};

[[nodiscard]] std::string format_rom_size(const std::size_t bytes) {
    constexpr double bytes_per_mib = 1024.0 * 1024.0;
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%.2f MiB", static_cast<double>(bytes) / bytes_per_mib);
    return buffer;
}

[[nodiscard]] ImTextureID texture_id(SDL_Texture* texture) noexcept {
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
}

} // namespace

Application::~Application() {
    shutdown();
}

int Application::run() {
    if (!initialize()) {
        shutdown();
        return 1;
    }

    while (!should_quit_) {
        process_events();
        process_file_dialog_result();
        update();
        render();
    }

    save_settings();
    shutdown();
    return 0;
}

bool Application::initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return false;
    }
    sdl_initialized_ = true;

    if (char* preference_path = SDL_GetPrefPath("SRGBA", "SRGBA")) {
        settings_path_ = std::filesystem::path(preference_path) / "settings.json";
        SDL_free(preference_path);
    } else {
        settings_path_ = std::filesystem::current_path() / "settings.json";
    }
    settings_ = Settings::load(settings_path_);
    emulator_.set_boot_mode(settings_.boot_through_bios ? core::BootMode::Bios
                                                        : core::BootMode::Direct);
    if (!settings_.bios_path.empty()) {
        std::string error;
        if (!emulator_.load_bios(settings_.bios_path, error)) {
            status_message_ = std::move(error);
            status_is_error_ = true;
        }
    }

    const auto window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    window_ = SDL_CreateWindow(kWindowTitle, settings_.window_width, settings_.window_height,
                               window_flags);
    if (!window_) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("Renderer creation failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_SetRenderVSync(renderer_, 1)) {
        SDL_Log("VSync could not be enabled: %s", SDL_GetError());
    }

    framebuffer_texture_ = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        static_cast<int>(core::kScreenWidth), static_cast<int>(core::kScreenHeight));
    if (!framebuffer_texture_) {
        SDL_Log("Framebuffer texture creation failed: %s", SDL_GetError());
        return false;
    }
    apply_scale_filter();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imgui_context_created_ = true;
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 7.0F;
    style.ChildRounding = 5.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 5.0F;
    style.ScrollbarRounding = 8.0F;
    style.WindowPadding = ImVec2(14.0F, 12.0F);

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_)) {
        SDL_Log("Dear ImGui SDL platform initialization failed.");
        return false;
    }
    imgui_platform_initialized_ = true;
    if (!ImGui_ImplSDLRenderer3_Init(renderer_)) {
        SDL_Log("Dear ImGui SDL renderer initialization failed.");
        return false;
    }
    imgui_renderer_initialized_ = true;

    return true;
}

void Application::shutdown() noexcept {
    if (imgui_renderer_initialized_) {
        ImGui_ImplSDLRenderer3_Shutdown();
        imgui_renderer_initialized_ = false;
    }
    if (imgui_platform_initialized_) {
        ImGui_ImplSDL3_Shutdown();
        imgui_platform_initialized_ = false;
    }
    if (imgui_context_created_) {
        ImGui::DestroyContext();
        imgui_context_created_ = false;
    }

    if (framebuffer_texture_) {
        SDL_DestroyTexture(framebuffer_texture_);
        framebuffer_texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdl_initialized_) {
        SDL_Quit();
        sdl_initialized_ = false;
    }
}

void Application::process_events() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);

        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            should_quit_ = true;
            continue;
        }

        if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
            continue;
        }

        const auto& io = ImGui::GetIO();
        if (io.WantCaptureKeyboard) {
            continue;
        }

        const auto modifiers = SDL_GetModState();
        if (event.key.key == SDLK_O && (modifiers & SDL_KMOD_CTRL) != 0) {
            request_open_rom();
        } else if (event.key.key == SDLK_SPACE && emulator_.has_rom()) {
            emulator_.set_paused(!emulator_.is_paused());
        } else if (event.key.key == SDLK_F11) {
            const auto current = SDL_GetWindowFlags(window_);
            const bool is_fullscreen = (current & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowFullscreen(window_, !is_fullscreen);
        }
    }
}

void Application::process_file_dialog_result() {
    FileDialogResult result;
    FileDialogPurpose purpose = FileDialogPurpose::None;
    {
        const std::scoped_lock lock(file_dialog_mutex_);
        if (!file_dialog_result_.completed) {
            return;
        }
        result = std::move(file_dialog_result_);
        file_dialog_result_ = {};
        file_dialog_open_ = false;
        purpose = file_dialog_purpose_;
        file_dialog_purpose_ = FileDialogPurpose::None;
    }

    if (!result.error_message.empty()) {
        status_message_ = std::move(result.error_message);
        status_is_error_ = true;
    } else if (result.selected_path && purpose == FileDialogPurpose::Bios) {
        load_bios(*result.selected_path);
    } else if (result.selected_path && purpose == FileDialogPurpose::Rom) {
        load_rom(*result.selected_path);
    }
}

void Application::update() {
    emulator_.run_frame();
    const auto& framebuffer = emulator_.framebuffer();
    const auto pitch = static_cast<int>(core::kScreenWidth * sizeof(core::Rgba8));
    if (!SDL_UpdateTexture(framebuffer_texture_, nullptr, framebuffer.data(), pitch)) {
        status_message_ = std::string("Framebuffer upload failed: ") + SDL_GetError();
        status_is_error_ = true;
    }
}

void Application::render() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    draw_menu_bar();
    draw_workspace();
    if (show_settings_) {
        draw_settings_window();
    }
    if (show_about_) {
        draw_about_window();
    }

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_, 8, 10, 17, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

void Application::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open ROM...", "Ctrl+O", false, !file_dialog_open_)) {
            request_open_rom();
        }
        if (ImGui::MenuItem("Load BIOS...", nullptr, false, !file_dialog_open_)) {
            request_open_bios();
        }
        if (ImGui::MenuItem("Unload BIOS", nullptr, false, emulator_.has_bios())) {
            emulator_.unload_bios();
            settings_.bios_path.clear();
            status_message_ = "BIOS unloaded; direct boot is active";
            status_is_error_ = false;
        }
        if (ImGui::MenuItem("Close ROM", nullptr, false, emulator_.has_rom())) {
            close_rom();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            should_quit_ = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Emulation")) {
        const char* pause_label = emulator_.is_paused() ? "Resume" : "Pause";
        if (ImGui::MenuItem(pause_label, "Space", false, emulator_.has_rom())) {
            emulator_.set_paused(!emulator_.is_paused());
        }
        if (ImGui::MenuItem("Reset", nullptr, false, emulator_.has_rom())) {
            emulator_.reset();
            status_message_ = emulator_.booting_through_bios() ? "ROM reset through BIOS"
                                                               : "ROM reset with direct boot";
            status_is_error_ = false;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Settings...")) {
            show_settings_ = true;
        }
        if (ImGui::MenuItem("Fullscreen", "F11")) {
            const auto current = SDL_GetWindowFlags(window_);
            SDL_SetWindowFullscreen(window_, (current & SDL_WINDOW_FULLSCREEN) == 0);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About SRGBA")) {
            show_about_ = true;
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void Application::draw_workspace() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("SRGBA Workspace", nullptr, flags);

    if (emulator_.has_rom()) {
        draw_game_view();
    } else {
        draw_landing_page();
    }

    ImGui::End();
}

void Application::draw_landing_page() {
    const auto available = ImGui::GetContentRegionAvail();
    const float content_width = std::min(620.0F, available.x);
    ImGui::SetCursorPosX(std::max(0.0F, (available.x - content_width) * 0.5F));
    ImGui::BeginChild("Landing", ImVec2(content_width, 0.0F), ImGuiChildFlags_None);

    ImGui::Dummy(ImVec2(0.0F, std::max(24.0F, available.y * 0.12F)));
    ImGui::SetWindowFontScale(2.2F);
    ImGui::TextUnformatted("SRGBA");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextDisabled("Game Boy Advance emulator - M2 bus and boot build");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Open a legally obtained .gba ROM to run it through the ARM7TDMI interpreter and GBA "
        "memory bus. Video output remains a diagnostic placeholder until the next milestone.");
    ImGui::Spacing();

    if (ImGui::Button("Open GBA ROM", ImVec2(190.0F, 42.0F))) {
        request_open_rom();
    }

    if (!settings_.recent_roms.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Recent games");
        for (std::size_t index = 0; index < settings_.recent_roms.size(); ++index) {
            const std::filesystem::path path(settings_.recent_roms[index]);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(path.filename().string().c_str())) {
                load_rom(path);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", settings_.recent_roms[index].c_str());
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    if (status_is_error_) {
        ImGui::TextColored(ImVec4(1.0F, 0.38F, 0.36F, 1.0F), "%s", status_message_.c_str());
    } else {
        ImGui::TextDisabled("%s", status_message_.c_str());
    }
    ImGui::EndChild();
}

void Application::draw_game_view() {
    const auto* header = emulator_.rom_header();
    const auto* path = emulator_.rom_path();
    if (!header || !path) {
        return;
    }

    const auto title = header->title.empty() ? path->filename().string() : header->title;
    ImGui::Text("%s", title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]  %s", header->game_code.empty() ? "----" : header->game_code.c_str(),
                        format_rom_size(emulator_.rom_size()).c_str());
    ImGui::SameLine();
    ImGui::TextColored(emulator_.is_paused() ? ImVec4(1.0F, 0.74F, 0.28F, 1.0F)
                                             : ImVec4(0.30F, 0.88F, 0.58F, 1.0F),
                       emulator_.is_paused() ? "PAUSED" : "RUNNING");

    if (!header->is_valid()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.25F, 1.0F),
                           "Header warning: fixed byte or checksum is invalid");
    }

    ImGui::Separator();
    ImGui::TextDisabled("M2 CPU/bus execution - placeholder video (PC %08X, %llu instructions)",
                        static_cast<unsigned>(emulator_.cpu().program_counter()),
                        static_cast<unsigned long long>(emulator_.instruction_counter()));

    const auto available = ImGui::GetContentRegionAvail();
    const auto width_scale = available.x / static_cast<float>(core::kScreenWidth);
    const auto height_scale = available.y / static_cast<float>(core::kScreenHeight);
    float scale = std::max(0.1F, std::min(width_scale, height_scale));
    if (settings_.integer_scaling && scale >= 1.0F) {
        scale = std::floor(scale);
    }

    const ImVec2 image_size{
        static_cast<float>(core::kScreenWidth) * scale,
        static_cast<float>(core::kScreenHeight) * scale,
    };
    const auto cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPosX(cursor.x + std::max(0.0F, (available.x - image_size.x) * 0.5F));
    ImGui::SetCursorPosY(cursor.y + std::max(0.0F, (available.y - image_size.y) * 0.5F));
    ImGui::Image(texture_id(framebuffer_texture_), image_size);
}

void Application::draw_settings_window() {
    ImGui::SetNextWindowSize(ImVec2(470.0F, 390.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &show_settings_)) {
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Video");
    ImGui::Checkbox("Integer scaling", &settings_.integer_scaling);

    int selected_filter = settings_.scale_filter == ScaleFilter::Nearest ? 0 : 1;
    if (ImGui::RadioButton("Nearest neighbor", &selected_filter, 0)) {
        settings_.scale_filter = ScaleFilter::Nearest;
        apply_scale_filter();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Linear", &selected_filter, 1)) {
        settings_.scale_filter = ScaleFilter::Linear;
        apply_scale_filter();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("System");
    int selected_boot = settings_.boot_through_bios ? 1 : 0;
    if (ImGui::RadioButton("Direct boot", &selected_boot, 0)) {
        settings_.boot_through_bios = false;
        emulator_.set_boot_mode(core::BootMode::Direct);
        status_message_ = "Direct post-BIOS development boot selected";
        status_is_error_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Use loaded BIOS", &selected_boot, 1)) {
        settings_.boot_through_bios = true;
        emulator_.set_boot_mode(core::BootMode::Bios);
        status_message_ = emulator_.has_bios() ? "BIOS boot selected"
                                               : "BIOS boot selected; load a 16 KiB BIOS image";
        status_is_error_ = false;
    }
    if (emulator_.has_bios()) {
        const std::filesystem::path bios_path(settings_.bios_path);
        ImGui::TextDisabled("BIOS: %s (CRC32 %08X)", bios_path.filename().string().c_str(),
                            static_cast<unsigned>(emulator_.bus().bios_crc32()));
    } else {
        ImGui::TextDisabled("No BIOS loaded; SRGBA will use direct boot.");
    }
    if (ImGui::Button("Choose BIOS...")) {
        request_open_bios();
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Input");
    ImGui::TextDisabled("Keyboard and controller remapping will be added with keypad emulation.");
    ImGui::BulletText("Ctrl+O: Open ROM");
    ImGui::BulletText("Space: Pause or resume");
    ImGui::BulletText("F11: Toggle fullscreen");

    ImGui::End();
}

void Application::draw_about_window() {
    ImGui::SetNextWindowSize(ImVec2(430.0F, 230.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("About SRGBA", &show_about_)) {
        ImGui::End();
        return;
    }

    ImGui::SetWindowFontScale(1.5F);
    ImGui::TextUnformatted("SRGBA 0.3.0");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextWrapped(
        "A clean-room Game Boy Advance emulator project built with C++20, SDL3, and Dear ImGui.");
    ImGui::Spacing();
    ImGui::TextDisabled("Current status: M2 ARM7TDMI bus and boot foundation");
    ImGui::TextDisabled("License: MIT");
    ImGui::Spacing();
    ImGui::TextWrapped("SRGBA does not include commercial ROMs or Nintendo BIOS files.");
    ImGui::End();
}

void Application::request_open_rom() {
    if (file_dialog_open_) {
        return;
    }
    file_dialog_open_ = true;
    file_dialog_purpose_ = FileDialogPurpose::Rom;
    SDL_ShowOpenFileDialog(&Application::file_dialog_callback, this, window_, kRomFilters,
                           static_cast<int>(std::size(kRomFilters)), nullptr, false);
}

void Application::request_open_bios() {
    if (file_dialog_open_) {
        return;
    }
    file_dialog_open_ = true;
    file_dialog_purpose_ = FileDialogPurpose::Bios;
    SDL_ShowOpenFileDialog(&Application::file_dialog_callback, this, window_, kBiosFilters,
                           static_cast<int>(std::size(kBiosFilters)), nullptr, false);
}

void Application::load_rom(const std::filesystem::path& path) {
    std::string error;
    if (!emulator_.load_rom(path, error)) {
        status_message_ = std::move(error);
        status_is_error_ = true;
        return;
    }

    settings_.add_recent_rom(path);
    const auto* header = emulator_.rom_header();
    const auto title = header && !header->title.empty() ? header->title : path.filename().string();
    SDL_SetWindowTitle(window_, (std::string(kWindowTitle) + " - " + title).c_str());
    status_message_ = emulator_.booting_through_bios() ? "ROM loaded at the BIOS reset vector"
                                                       : "ROM loaded with direct boot";
    status_is_error_ = false;
}

void Application::load_bios(const std::filesystem::path& path) {
    std::string error;
    if (!emulator_.load_bios(path, error)) {
        status_message_ = std::move(error);
        status_is_error_ = true;
        return;
    }

    settings_.bios_path = path.lexically_normal().string();
    status_message_ = "BIOS loaded and validated";
    status_is_error_ = false;
}

void Application::close_rom() {
    emulator_.unload_rom();
    SDL_SetWindowTitle(window_, kWindowTitle);
    status_message_ = "ROM closed";
    status_is_error_ = false;
}

void Application::apply_scale_filter() const noexcept {
    if (!framebuffer_texture_) {
        return;
    }
    const auto mode = settings_.scale_filter == ScaleFilter::Nearest ? SDL_SCALEMODE_NEAREST
                                                                     : SDL_SCALEMODE_LINEAR;
    SDL_SetTextureScaleMode(framebuffer_texture_, mode);
}

void Application::save_settings() noexcept {
    if (window_) {
        SDL_GetWindowSize(window_, &settings_.window_width, &settings_.window_height);
    }
    settings_.save(settings_path_);
}

void SDLCALL Application::file_dialog_callback(void* userdata, const char* const* file_list,
                                               const int selected_filter) {
    static_cast<void>(selected_filter);
    auto& application = *static_cast<Application*>(userdata);
    const std::scoped_lock lock(application.file_dialog_mutex_);

    application.file_dialog_result_.completed = true;
    if (!file_list) {
        application.file_dialog_result_.error_message =
            std::string("File dialog failed: ") + SDL_GetError();
    } else if (*file_list) {
        application.file_dialog_result_.selected_path = std::filesystem::path(*file_list);
    }
}

} // namespace srgba::app
