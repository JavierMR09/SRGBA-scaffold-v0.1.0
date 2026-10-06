#include "app/application.hpp"

#include "app/paths.hpp"
#include "srgba/core/framebuffer.hpp"

#include <SDL3/SDL_dialog.h>

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <system_error>
#include <utility>

namespace srgba::app {
namespace {

constexpr const char* kWindowTitle = "SRGBA";
#ifndef SRGBA_VERSION
#define SRGBA_VERSION "dev"
#endif
constexpr const char* kVersion = SRGBA_VERSION;

// One GBA frame is 280,896 cycles of the 16.78 MHz master clock (~59.73 Hz).
constexpr std::uint64_t kNanosecondsPerFrame = 280896ULL * 1000000000ULL / 16777216ULL;
constexpr std::uint64_t kMaxCatchUpFrames = 4;
// Unlimited fast-forward runs frames for at most this long per displayed frame; fixed-speed
// fast-forward gives up on catching up after the longer budget so the UI stays responsive on
// machines that cannot reach the chosen speed.
constexpr std::uint64_t kUnlimitedFastForwardBudgetNs = 12000000ULL;
constexpr std::uint64_t kFastForwardBudgetNs = 30000000ULL;
constexpr std::uint64_t kNoticeDurationNs = 2500000000ULL;
constexpr SDL_DialogFileFilter kRomFilters[] = {
    {"Game Boy Advance ROMs", "gba;agb"},
    {"All files", "*"},
};
constexpr SDL_DialogFileFilter kBiosFilters[] = {
    {"Game Boy Advance BIOS images", "bin;rom"},
    {"All files", "*"},
};

const ImVec4 kErrorColor{1.0F, 0.38F, 0.36F, 1.0F};
const ImVec4 kGoodColor{0.30F, 0.88F, 0.58F, 1.0F};
const ImVec4 kWarningColor{1.0F, 0.74F, 0.28F, 1.0F};

[[nodiscard]] std::string format_rom_size(const std::uintmax_t bytes) {
    constexpr double bytes_per_kib = 1024.0;
    constexpr double bytes_per_mib = 1024.0 * 1024.0;
    char buffer[32]{};
    if (static_cast<double>(bytes) < bytes_per_mib) {
        std::snprintf(buffer, sizeof(buffer), "%.0f KiB",
                      std::ceil(static_cast<double>(bytes) / bytes_per_kib));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MiB",
                      static_cast<double>(bytes) / bytes_per_mib);
    }
    return buffer;
}

[[nodiscard]] const char* describe_display_mode(const std::uint16_t control) noexcept {
    if ((control & 0x0080U) != 0U) {
        return "Forced blank";
    }
    switch (control & 0x7U) {
    case 0:
        return "Mode 0 (4 tile layers)";
    case 1:
        return "Mode 1 (2 tile + 1 affine)";
    case 2:
        return "Mode 2 (2 affine layers)";
    case 3:
        return "Mode 3 (bitmap)";
    case 4:
        return "Mode 4 (paletted bitmap)";
    case 5:
        return "Mode 5 (small bitmap)";
    default:
        return "Invalid mode";
    }
}

[[nodiscard]] ImTextureID texture_id(SDL_Texture* texture) noexcept {
    return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture));
}

// "2026-10-05 14:32" for a slot file, or nullopt when the slot is empty.
[[nodiscard]] std::optional<std::string> slot_timestamp(const std::filesystem::path& path) {
    std::error_code error;
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return std::nullopt;
    }
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        modified - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    const auto time = std::chrono::system_clock::to_time_t(system_time);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[64]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return std::string(buffer);
}

[[nodiscard]] bool contains_ignoring_case(const std::string& text, const std::string& query) {
    if (query.empty()) {
        return true;
    }
    const auto lower = [](const char character) {
        return std::tolower(static_cast<unsigned char>(character));
    };
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
                       [&](const char left, const char right) {
                           return lower(left) == lower(right);
                       }) != text.end();
}

[[nodiscard]] const char* fast_forward_label(const int speed) {
    switch (speed) {
    case 0:
        return "max";
    case 2:
        return "2x";
    case 3:
        return "3x";
    case 4:
        return "4x";
    case 8:
        return "8x";
    default:
        return "";
    }
}

} // namespace

Application::~Application() {
    shutdown();
}

int Application::run(const std::optional<std::filesystem::path>& initial_rom) {
    if (!initialize()) {
        shutdown();
        return 1;
    }
    if (initial_rom) {
        load_rom(*initial_rom);
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
        settings_path_ = path_from_utf8(preference_path) / "settings.json";
        SDL_free(preference_path);
    } else {
        settings_path_ = std::filesystem::current_path() / "settings.json";
    }
    settings_ = Settings::load(settings_path_);
    input_.set_bindings(InputBindings::from_settings(settings_));
    apply_rewind_settings();
    emulator_.set_boot_mode(settings_.boot_through_bios ? core::BootMode::Bios
                                                        : core::BootMode::Direct);
    if (!settings_.bios_path.empty()) {
        std::string error;
        if (!emulator_.load_bios(path_from_utf8(settings_.bios_path), error)) {
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
    // Menu navigation must not swallow emulator hotkeys such as Space while a game is paused.
    io.ConfigNavCaptureKeyboard = false;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 7.0F;
    style.ChildRounding = 5.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 5.0F;
    style.ScrollbarRounding = 8.0F;
    style.WindowPadding = ImVec2(14.0F, 12.0F);
    // Opaque panels keep settings and cheats readable over the game.
    style.Colors[ImGuiCol_WindowBg].w = 1.0F;
    style.Colors[ImGuiCol_PopupBg].w = 1.0F;

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

    initialize_audio();
    configure_smoke_test();
    rescan_library();
    return true;
}

void Application::configure_smoke_test() {
    // Lets CI and developers capture the UI headlessly, e.g. with SDL_VIDEO_DRIVER=offscreen.
    if (const char* path = SDL_getenv("SRGBA_SCREENSHOT_PATH")) {
        smoke_test_.screenshot_path = path;
        smoke_test_.capture_after_frames = 90;
        if (const char* frames = SDL_getenv("SRGBA_SCREENSHOT_FRAMES")) {
            smoke_test_.capture_after_frames = std::strtoull(frames, nullptr, 10);
        }
    }
    if (const char* panels = SDL_getenv("SRGBA_OPEN_PANELS")) {
        const std::string list = panels;
        show_cheats_ = list.find("cheats") != std::string::npos;
        show_about_ = list.find("about") != std::string::npos;
        show_settings_ = list.find("settings") != std::string::npos;
        if (list.find("controls") != std::string::npos) {
            show_settings_ = true;
            requested_settings_tab_ = SettingsTab::Controls;
        } else if (list.find("emulation") != std::string::npos) {
            show_settings_ = true;
            requested_settings_tab_ = SettingsTab::Emulation;
        }
    }
}

void Application::initialize_audio() noexcept {
    // SDL converts the GBA's 32,768 Hz stereo stream to the output device's format and rate.
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(core::Apu::kSampleRate)};
    audio_stream_ =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!audio_stream_) {
        SDL_Log("Audio output is unavailable: %s", SDL_GetError());
        return;
    }
    apply_audio_gain();
    SDL_ResumeAudioStreamDevice(audio_stream_);
}

void Application::apply_audio_gain() const noexcept {
    if (audio_stream_) {
        const auto gain =
            settings_.audio_muted ? 0.0F : static_cast<float>(settings_.audio_volume) / 100.0F;
        SDL_SetAudioStreamGain(audio_stream_, gain);
    }
}

void Application::update_audio(const bool audible) {
    audio_buffer_.clear();
    emulator_.take_audio_samples(audio_buffer_);
    if (!audio_stream_) {
        return;
    }
    if (!audible) {
        SDL_ClearAudioStream(audio_stream_);
        return;
    }
    // Keep latency bounded: if the device fell behind (for example while the window was being
    // dragged), drop this batch instead of letting the queue grow.
    constexpr int kBytesPerFrame = 4; // stereo int16
    constexpr int kMaximumQueuedBytes =
        static_cast<int>(core::Apu::kSampleRate) / 8 * kBytesPerFrame;
    if (SDL_GetAudioStreamQueued(audio_stream_) > kMaximumQueuedBytes || audio_buffer_.empty()) {
        return;
    }
    SDL_PutAudioStreamData(audio_stream_, audio_buffer_.data(),
                           static_cast<int>(audio_buffer_.size() * sizeof(std::int16_t)));
}

void Application::update_save_status() {
    if (emulator_.saves_written() != observed_saves_written_) {
        observed_saves_written_ = emulator_.saves_written();
        show_notice("Game saved");
    }
    if (!emulator_.save_error().empty() && !status_is_error_) {
        status_message_ = emulator_.save_error();
        status_is_error_ = true;
    }
}

void Application::show_notice(std::string text, const bool error) {
    notice_ = std::move(text);
    notice_is_error_ = error;
    notice_until_ns_ = SDL_GetTicksNS() + kNoticeDurationNs;
}

void Application::shutdown() noexcept {
    library_.stop();
    input_.close_all();
    if (audio_stream_) {
        SDL_DestroyAudioStream(audio_stream_);
        audio_stream_ = nullptr;
    }
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

    if (filtered_texture_) {
        SDL_DestroyTexture(filtered_texture_);
        filtered_texture_ = nullptr;
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
        input_.handle_event(event);
        if (process_binding_capture(event)) {
            continue;
        }
        ImGui_ImplSDL3_ProcessEvent(&event);

        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            should_quit_ = true;
            break;
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data != nullptr) {
                handle_dropped_file(path_from_utf8(event.drop.data));
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if (!event.key.repeat && !ImGui::GetIO().WantTextInput) {
                handle_key_down(event.key);
            }
            break;
        default:
            break;
        }
    }
}

bool Application::process_binding_capture(const SDL_Event& event) {
    if (capture_kind_ == CaptureKind::None) {
        return false;
    }
    auto bindings = input_.bindings();
    if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.repeat) {
            return true;
        }
        if (event.key.scancode != SDL_SCANCODE_ESCAPE && capture_kind_ == CaptureKind::Keyboard) {
            // A key drives one GBA button; taking it here removes it elsewhere.
            for (auto& keys : bindings.keyboard) {
                std::replace(keys.begin(), keys.end(), event.key.scancode, SDL_SCANCODE_UNKNOWN);
            }
            bindings.keyboard[capture_button_][capture_slot_] = event.key.scancode;
            apply_bindings(bindings);
        }
        capture_kind_ = CaptureKind::None;
        return true;
    }
    if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && capture_kind_ == CaptureKind::Gamepad) {
        const auto button = static_cast<SDL_GamepadButton>(event.gbutton.button);
        for (auto& buttons : bindings.gamepad) {
            std::replace(buttons.begin(), buttons.end(), button, SDL_GAMEPAD_BUTTON_INVALID);
        }
        bindings.gamepad[capture_button_][capture_slot_] = button;
        apply_bindings(bindings);
        capture_kind_ = CaptureKind::None;
        return true;
    }
    // Swallow other gamepad buttons and keys while waiting, so they do not drive the menus.
    return event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_KEY_UP;
}

void Application::handle_key_down(const SDL_KeyboardEvent& key) {
    const bool control = (key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    if (key.key >= SDLK_F1 && key.key <= SDLK_F9) {
        const auto slot = static_cast<int>(key.key - SDLK_F1) + 1;
        if (shift) {
            save_state_slot(slot);
        } else {
            load_state_slot(slot);
        }
        return;
    }
    switch (key.key) {
    case SDLK_O:
        if (control) {
            request_open_rom();
        }
        break;
    case SDLK_R:
        if (control) {
            reset_game();
        }
        break;
    case SDLK_SPACE:
        toggle_pause();
        break;
    case SDLK_N:
        advance_frame();
        break;
    case SDLK_M:
        settings_.audio_muted = !settings_.audio_muted;
        apply_audio_gain();
        show_notice(settings_.audio_muted ? "Audio muted" : "Audio unmuted");
        break;
    case SDLK_F11: {
        const auto current = SDL_GetWindowFlags(window_);
        SDL_SetWindowFullscreen(window_, (current & SDL_WINDOW_FULLSCREEN) == 0);
        break;
    }
    default:
        break;
    }
}

void Application::handle_dropped_file(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        add_rom_folder(path);
    } else {
        load_rom(path);
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
        show_notice(status_message_, true);
    } else if (result.selected_path) {
        switch (purpose) {
        case FileDialogPurpose::Bios:
            load_bios(*result.selected_path);
            break;
        case FileDialogPurpose::Rom:
            load_rom(*result.selected_path);
            break;
        case FileDialogPurpose::RomFolder:
            add_rom_folder(*result.selected_path);
            break;
        case FileDialogPurpose::None:
            break;
        }
    }
}

void Application::update() {
    const auto now = SDL_GetTicksNS();
    const auto elapsed = last_update_ns_ == 0 ? std::uint64_t{0} : now - last_update_ns_;
    last_update_ns_ = now;

    auto& io = ImGui::GetIO();
    const bool playing = emulator_.state() == core::RunState::Running;
    // While a game runs, controller and arrow-key input belong to the game, not menu navigation.
    if (playing) {
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard);
    } else {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    }
    const bool keyboard_free = !io.WantTextInput && capture_kind_ == CaptureKind::None;
    emulator_.set_pressed_keys(input_.pressed_keys(keyboard_free));

    run_emulation(now, elapsed);
    update_audio(emulator_.state() == core::RunState::Running && !fast_forward_active_ &&
                 !rewinding_);
    update_save_status();

    if (library_.revision() != library_revision_) {
        library_revision_ = library_.revision();
        library_entries_ = library_.entries();
    }

    // Emulated frames per second, refreshed twice a second.
    if (now - fps_window_start_ns_ >= 500000000ULL) {
        const auto window = static_cast<double>(now - fps_window_start_ns_) / 1e9;
        measured_fps_ = static_cast<double>(frames_this_window_) / window;
        frames_this_window_ = 0;
        fps_window_start_ns_ = now;
    }

    if (const auto& fault = emulator_.fault(); fault && !fault_reported_) {
        char message[160]{};
        std::snprintf(message, sizeof(message),
                      "Emulation stopped: unsupported %s instruction %0*X at %08X",
                      fault->instruction_set == core::InstructionSet::Arm ? "ARM" : "Thumb",
                      fault->instruction_set == core::InstructionSet::Arm ? 8 : 4,
                      static_cast<unsigned>(fault->opcode), static_cast<unsigned>(fault->address));
        status_message_ = message;
        status_is_error_ = true;
        fault_reported_ = true;
    } else if (!fault) {
        fault_reported_ = false;
    }

    upload_framebuffer();
}

void Application::run_emulation(const std::uint64_t now, const std::uint64_t elapsed) {
    const auto& io = ImGui::GetIO();
    const bool hotkeys = !io.WantTextInput && capture_kind_ == CaptureKind::None;
    int key_count = 0;
    const bool* keyboard = SDL_GetKeyboardState(&key_count);
    const auto key_held = [&](const SDL_Scancode scancode) {
        return hotkeys && keyboard != nullptr && static_cast<int>(scancode) < key_count &&
               keyboard[scancode];
    };
    const bool rewind_held =
        key_held(SDL_SCANCODE_GRAVE) || input_.trigger_held(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    const bool fast_forward_held =
        key_held(SDL_SCANCODE_TAB) || input_.trigger_held(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

    // Rewind steps back two frames per displayed frame (double speed in reverse).
    rewinding_ = rewind_held && emulator_.has_rom() && emulator_.rewind_enabled();
    if (rewinding_) {
        frame_time_accumulator_ns_ = 0;
        fast_forward_active_ = false;
        rewind_accumulator_ns_ =
            std::min(rewind_accumulator_ns_ + elapsed, kNanosecondsPerFrame * kMaxCatchUpFrames);
        while (rewind_accumulator_ns_ >= kNanosecondsPerFrame) {
            rewind_accumulator_ns_ -= kNanosecondsPerFrame;
            if (!emulator_.rewind_step()) {
                if (!rewind_exhausted_) {
                    show_notice("Reached the oldest rewind point");
                }
                rewind_exhausted_ = true;
                rewind_accumulator_ns_ = 0;
                break;
            }
            framebuffer_dirty_ = true;
        }
        return;
    }
    rewind_accumulator_ns_ = 0;
    rewind_exhausted_ = false;

    const bool playing = emulator_.state() == core::RunState::Running;
    fast_forward_active_ = playing && (fast_forward_locked_ || fast_forward_held);
    if (!playing) {
        frame_time_accumulator_ns_ = 0;
        return;
    }

    if (fast_forward_active_ && settings_.fast_forward_speed == 0) {
        frame_time_accumulator_ns_ = 0;
        do {
            emulator_.run_frame();
            framebuffer_dirty_ = true;
            ++frames_this_window_;
        } while (emulator_.state() == core::RunState::Running &&
                 SDL_GetTicksNS() - now < kUnlimitedFastForwardBudgetNs);
        return;
    }

    // Run emulated frames at the GBA's own refresh rate (times the fast-forward speed)
    // regardless of the monitor's.
    const auto speed = static_cast<std::uint64_t>(
        fast_forward_active_ ? std::max(settings_.fast_forward_speed, 2) : 1);
    frame_time_accumulator_ns_ = std::min(frame_time_accumulator_ns_ + elapsed * speed,
                                          kNanosecondsPerFrame * kMaxCatchUpFrames * speed);
    while (frame_time_accumulator_ns_ >= kNanosecondsPerFrame &&
           emulator_.state() == core::RunState::Running) {
        emulator_.run_frame();
        frame_time_accumulator_ns_ -= kNanosecondsPerFrame;
        framebuffer_dirty_ = true;
        ++frames_this_window_;
        if (fast_forward_active_ && SDL_GetTicksNS() - now > kFastForwardBudgetNs) {
            frame_time_accumulator_ns_ = 0;
            break;
        }
    }
}

void Application::upload_framebuffer() {
    if (!framebuffer_dirty_) {
        return;
    }
    framebuffer_dirty_ = false;
    display_dirty_ = true;
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
    if (show_cheats_) {
        draw_cheats_window();
    }
    if (show_about_) {
        draw_about_window();
    }

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_, 8, 10, 17, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    capture_screenshot();
    SDL_RenderPresent(renderer_);
}

void Application::capture_screenshot() {
    if (smoke_test_.screenshot_path.empty()) {
        return;
    }
    if (++smoke_test_.presented_frames < smoke_test_.capture_after_frames) {
        return;
    }
    if (SDL_Surface* surface = SDL_RenderReadPixels(renderer_, nullptr)) {
        if (!SDL_SaveBMP(surface, smoke_test_.screenshot_path.c_str())) {
            SDL_Log("Screenshot failed: %s", SDL_GetError());
        }
        SDL_DestroySurface(surface);
    } else {
        SDL_Log("Screenshot failed: %s", SDL_GetError());
    }
    smoke_test_.screenshot_path.clear();
    should_quit_ = true;
}

void Application::draw_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    const bool has_rom = emulator_.has_rom();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open ROM...", "Ctrl+O", false, !file_dialog_open_)) {
            request_open_rom();
        }
        if (ImGui::BeginMenu("Recent games", !settings_.recent_roms.empty())) {
            const auto recent = settings_.recent_roms; // load_rom reorders the list
            for (std::size_t index = 0; index < recent.size(); ++index) {
                const auto path = path_from_utf8(recent[index]);
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::MenuItem(path_to_utf8(path.filename()).c_str())) {
                    load_rom(path);
                }
                ImGui::SetItemTooltip("%s", recent[index].c_str());
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Add ROM folder to library...", nullptr, false, !file_dialog_open_)) {
            request_add_rom_folder();
        }
        if (ImGui::MenuItem("Close ROM", nullptr, false, has_rom)) {
            close_rom();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Load BIOS...", nullptr, false, !file_dialog_open_)) {
            request_open_bios();
        }
        if (ImGui::MenuItem("Unload BIOS", nullptr, false, emulator_.has_bios())) {
            emulator_.unload_bios();
            settings_.bios_path.clear();
            status_message_ = "BIOS unloaded; direct boot is active";
            status_is_error_ = false;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) {
            should_quit_ = true;
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Emulation")) {
        const char* pause_label = emulator_.is_paused() ? "Resume" : "Pause";
        if (ImGui::MenuItem(pause_label, "Space", false, has_rom)) {
            toggle_pause();
        }
        if (ImGui::MenuItem("Advance one frame", "N", false, has_rom)) {
            advance_frame();
        }
        if (ImGui::MenuItem("Fast forward", "Tab (hold)", fast_forward_locked_, has_rom)) {
            fast_forward_locked_ = !fast_forward_locked_;
        }
        if (ImGui::BeginMenu("Fast-forward speed")) {
            for (const int speed : {2, 3, 4, 8, 0}) {
                if (ImGui::MenuItem(speed == 0 ? "As fast as possible" : fast_forward_label(speed),
                                    nullptr, settings_.fast_forward_speed == speed)) {
                    settings_.fast_forward_speed = speed;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Rewind", "` (hold)", settings_.rewind_enabled)) {
            settings_.rewind_enabled = !settings_.rewind_enabled;
            apply_rewind_settings();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset", "Ctrl+R", false, has_rom)) {
            reset_game();
        }
        if (ImGui::MenuItem("Mute audio", "M", settings_.audio_muted)) {
            settings_.audio_muted = !settings_.audio_muted;
            apply_audio_gain();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("States", has_rom)) {
        if (ImGui::BeginMenu("Save state")) {
            draw_state_menu(true);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Load state")) {
            draw_state_menu(false);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Cheats...", nullptr, show_cheats_)) {
            show_cheats_ = !show_cheats_;
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
        ImGui::Separator();
        if (ImGui::MenuItem("GBA LCD colors", nullptr, settings_.color_correction)) {
            settings_.color_correction = !settings_.color_correction;
            display_dirty_ = true;
        }
        if (ImGui::BeginMenu("Screen filter")) {
            constexpr std::array<std::pair<core::LcdFilter, const char*>, 3> kFilters{{
                {core::LcdFilter::None, "None"},
                {core::LcdFilter::Grid, "LCD grid"},
                {core::LcdFilter::Scanlines, "Scanlines"},
            }};
            for (const auto& [filter, label] : kFilters) {
                if (ImGui::MenuItem(label, nullptr, settings_.lcd_filter == filter)) {
                    settings_.lcd_filter = filter;
                    display_dirty_ = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls and hotkeys")) {
            show_settings_ = true;
            requested_settings_tab_ = SettingsTab::Controls;
        }
        if (ImGui::MenuItem("About SRGBA")) {
            show_about_ = true;
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void Application::draw_state_menu(const bool save) {
    for (int slot = 1; slot <= core::Emulator::kStateSlotCount; ++slot) {
        const auto timestamp = slot_timestamp(emulator_.state_slot_path(slot));
        char label[96]{};
        std::snprintf(label, sizeof(label), "Slot %d   %s", slot,
                      timestamp ? timestamp->c_str() : "(empty)");
        char shortcut[16]{};
        std::snprintf(shortcut, sizeof(shortcut), save ? "Shift+F%d" : "F%d", slot);
        if (ImGui::MenuItem(label, shortcut, false, save || timestamp.has_value())) {
            if (save) {
                save_state_slot(slot);
            } else {
                load_state_slot(slot);
            }
        }
    }
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
    const float content_width = std::min(940.0F, available.x);
    ImGui::SetCursorPosX(std::max(0.0F, (available.x - content_width) * 0.5F));
    ImGui::BeginChild("Landing", ImVec2(content_width, 0.0F), ImGuiChildFlags_None);

    ImGui::Dummy(ImVec2(0.0F, std::max(12.0F, available.y * 0.03F)));
    ImGui::SetWindowFontScale(2.2F);
    ImGui::TextUnformatted("SRGBA");
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextDisabled("Game Boy Advance emulator %s", kVersion);
    ImGui::Spacing();
    ImGui::TextWrapped("Open a legally obtained .gba ROM, drop one on this window, or add your ROM "
                       "folders to build a library. Try the demos in the samples folder to get "
                       "started.");
    ImGui::Spacing();

    if (ImGui::Button("Open GBA ROM", ImVec2(170.0F, 38.0F))) {
        request_open_rom();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add ROM folder", ImVec2(170.0F, 38.0F))) {
        request_add_rom_folder();
    }

    if (!status_message_.empty() && status_message_ != "Ready") {
        ImGui::Spacing();
        if (status_is_error_) {
            ImGui::TextColored(kErrorColor, "%s", status_message_.c_str());
        } else {
            ImGui::TextDisabled("%s", status_message_.c_str());
        }
    }

    if (!settings_.recent_roms.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Recent games");
        const auto recent = settings_.recent_roms; // load_rom reorders the list
        for (std::size_t index = 0; index < recent.size() && index < 5U; ++index) {
            const auto path = path_from_utf8(recent[index]);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(path_to_utf8(path.filename()).c_str())) {
                load_rom(path);
            }
            ImGui::SetItemTooltip("%s", recent[index].c_str());
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    draw_library();
    ImGui::EndChild();
}

void Application::draw_library() {
    char heading[64]{};
    std::snprintf(heading, sizeof(heading), "Library (%zu)", library_entries_.size());
    ImGui::SeparatorText(heading);

    if (settings_.rom_folders.empty()) {
        ImGui::TextDisabled("Add a folder of ROMs to browse your whole collection here.");
        return;
    }

    ImGui::SetNextItemWidth(260.0F);
    ImGui::InputTextWithHint("##search", "Search title, code or file", &library_filter_);
    ImGui::SameLine();
    if (ImGui::Button("Rescan")) {
        rescan_library();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Include subfolders", &settings_.scan_subfolders)) {
        rescan_library();
    }
    if (library_.scanning()) {
        ImGui::SameLine();
        ImGui::TextDisabled("Scanning %zu / %zu...", library_.files_examined(),
                            library_.files_found());
    }

    if (ImGui::TreeNode("Folders")) {
        std::optional<std::size_t> removed;
        for (std::size_t index = 0; index < settings_.rom_folders.size(); ++index) {
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::SmallButton("Remove")) {
                removed = index;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(settings_.rom_folders[index].c_str());
            ImGui::PopID();
        }
        if (removed) {
            settings_.rom_folders.erase(settings_.rom_folders.begin() +
                                        static_cast<std::ptrdiff_t>(*removed));
            rescan_library();
        }
        ImGui::TreePop();
    }

    std::vector<const core::RomInfo*> visible;
    visible.reserve(library_entries_.size());
    for (const auto& entry : library_entries_) {
        const auto file_name = path_to_utf8(entry.path.filename());
        if (contains_ignoring_case(entry.title, library_filter_) ||
            contains_ignoring_case(entry.game_code, library_filter_) ||
            contains_ignoring_case(file_name, library_filter_)) {
            visible.push_back(&entry);
        }
    }
    if (visible.empty()) {
        ImGui::TextDisabled(library_.scanning() ? "Looking for ROMs..."
                                                : "No ROMs match. Double-check the folders above.");
        return;
    }

    const auto rows_height =
        ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(visible.size()) + 1.6F);
    const float table_height =
        std::min(rows_height, std::max(180.0F, ImGui::GetContentRegionAvail().y - 4.0F));
    constexpr auto table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                 ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                 ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("LibraryTable", 5, table_flags, ImVec2(0.0F, table_height))) {
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch, 3.0F);
    ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthStretch, 0.8F);
    ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch, 1.2F);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch, 2.6F);
    ImGui::TableHeadersRow();

    std::optional<std::filesystem::path> launch;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& entry = *visible[static_cast<std::size_t>(row)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(row);
            if (ImGui::Selectable(entry.title.c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                launch = entry.path;
            }
            if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                launch = entry.path;
            }
            ImGui::SetItemTooltip("%s\nDouble-click to play", path_to_utf8(entry.path).c_str());
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.game_code.empty() ? "----" : entry.game_code.c_str());
            ImGui::TableSetColumnIndex(2);
            const auto save = core::save_type_name(entry.save_type);
            ImGui::TextDisabled("%.*s", static_cast<int>(save.size()), save.data());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s", format_rom_size(entry.size).c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("%s", path_to_utf8(entry.path.filename()).c_str());
        }
    }
    ImGui::EndTable();
    if (launch) {
        load_rom(*launch);
    }
}

void Application::draw_game_view() {
    const auto* header = emulator_.rom_header();
    const auto* path = emulator_.rom_path();
    if (!header || !path) {
        return;
    }

    const auto title = header->title.empty() ? path_to_utf8(path->filename()) : header->title;
    ImGui::Text("%s", title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]  %s", header->game_code.empty() ? "----" : header->game_code.c_str(),
                        format_rom_size(emulator_.rom_size()).c_str());
    ImGui::SameLine();
    ImGui::TextColored(emulator_.is_paused() ? kWarningColor : kGoodColor,
                       emulator_.is_paused() ? "PAUSED" : "RUNNING");
    if (!header->is_valid()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.55F, 0.25F, 1.0F),
                           "Header warning: fixed byte or checksum is invalid");
    }

    ImGui::Separator();
    const auto display_control = emulator_.bus().io_register16(0x000U);
    ImGui::TextDisabled("%s  |  %.1f fps  |  frame %llu  |  PC %08X%s",
                        describe_display_mode(display_control), measured_fps_,
                        static_cast<unsigned long long>(emulator_.frame_counter()),
                        static_cast<unsigned>(emulator_.cpu().program_counter()),
                        emulator_.is_halted() ? " (halted)" : "");
    if (emulator_.fault()) {
        ImGui::TextColored(kErrorColor, "%s", status_message_.c_str());
    } else if (!emulator_.save_error().empty()) {
        ImGui::TextColored(kErrorColor, "Save error: %s", emulator_.save_error().c_str());
    } else if (SDL_GetTicksNS() < notice_until_ns_) {
        ImGui::SameLine();
        ImGui::TextColored(notice_is_error_ ? kErrorColor : kGoodColor, "  %s", notice_.c_str());
    }

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
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(image_size);
    // Drawn as horizontal strips: SDL's software renderer (the fallback on machines without a
    // usable GPU) rejects single large textured triangles because of fixed-point overflow.
    const auto texture = texture_id(display_texture(scale));
    auto* draw_list = ImGui::GetWindowDrawList();
    constexpr int kStrips = 16;
    for (int strip = 0; strip < kStrips; ++strip) {
        const auto top = static_cast<float>(strip) / static_cast<float>(kStrips);
        const auto bottom = static_cast<float>(strip + 1) / static_cast<float>(kStrips);
        draw_list->AddImage(texture, ImVec2(origin.x, origin.y + image_size.y * top),
                            ImVec2(origin.x + image_size.x, origin.y + image_size.y * bottom),
                            ImVec2(0.0F, top), ImVec2(1.0F, bottom));
    }
    draw_speed_overlay(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
}

void Application::draw_speed_overlay(const ImVec2 image_min, const ImVec2 image_max) const {
    static_cast<void>(image_max);
    char label[48]{};
    if (rewinding_) {
        std::snprintf(label, sizeof(label), "<< Rewind");
    } else if (fast_forward_active_) {
        std::snprintf(label, sizeof(label), ">> Fast forward %s",
                      fast_forward_label(settings_.fast_forward_speed));
    } else if (emulator_.is_paused()) {
        std::snprintf(label, sizeof(label), "|| Paused  (N: next frame)");
    } else {
        return;
    }
    auto* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 padding{8.0F, 4.0F};
    const ImVec2 position{image_min.x + 10.0F, image_min.y + 10.0F};
    const auto text_size = ImGui::CalcTextSize(label);
    draw_list->AddRectFilled(position,
                             ImVec2(position.x + text_size.x + padding.x * 2.0F,
                                    position.y + text_size.y + padding.y * 2.0F),
                             IM_COL32(0, 0, 0, 170), 5.0F);
    draw_list->AddText(ImVec2(position.x + padding.x, position.y + padding.y),
                       IM_COL32(255, 255, 255, 235), label);
}

SDL_Texture* Application::display_texture(const float display_scale) {
    const bool filtered =
        settings_.color_correction || settings_.lcd_filter != core::LcdFilter::None;
    if (!filtered) {
        return framebuffer_texture_;
    }

    // Masks are drawn at the screen's own resolution so grid lines stay one pixel wide.
    int scale = 1;
    if (settings_.lcd_filter != core::LcdFilter::None) {
        const auto pixels = display_scale * ImGui::GetIO().DisplayFramebufferScale.x;
        scale = std::clamp(static_cast<int>(std::lround(pixels)), 2, core::kMaximumDisplayScale);
    }
    if (!filtered_texture_ || filtered_texture_scale_ != scale) {
        if (filtered_texture_) {
            SDL_DestroyTexture(filtered_texture_);
        }
        filtered_texture_ =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                              static_cast<int>(core::kScreenWidth) * scale,
                              static_cast<int>(core::kScreenHeight) * scale);
        if (!filtered_texture_) {
            return framebuffer_texture_;
        }
        filtered_texture_scale_ = scale;
        apply_scale_filter();
        display_dirty_ = true;
    }
    if (display_dirty_) {
        display_dirty_ = false;
        core::render_display(
            emulator_.framebuffer(), settings_.color_correction ? &color_correction_ : nullptr,
            settings_.lcd_filter, scale, static_cast<float>(settings_.filter_strength) / 100.0F,
            filtered_pixels_);
        const auto pitch = static_cast<int>(core::kScreenWidth * static_cast<std::size_t>(scale) *
                                            sizeof(core::Rgba8));
        SDL_UpdateTexture(filtered_texture_, nullptr, filtered_pixels_.data(), pitch);
    }
    return filtered_texture_;
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

void Application::request_add_rom_folder() {
    if (file_dialog_open_) {
        return;
    }
    file_dialog_open_ = true;
    file_dialog_purpose_ = FileDialogPurpose::RomFolder;
    SDL_ShowOpenFolderDialog(&Application::file_dialog_callback, this, window_, nullptr, false);
}

void Application::load_rom(const std::filesystem::path& path) {
    std::string error;
    if (!emulator_.load_rom(path, error)) {
        status_message_ = std::move(error);
        status_is_error_ = true;
        show_notice(status_message_, true);
        return;
    }

    settings_.add_recent_rom(path);
    const auto* header = emulator_.rom_header();
    const auto title =
        header && !header->title.empty() ? header->title : path_to_utf8(path.filename());
    SDL_SetWindowTitle(window_, (std::string(kWindowTitle) + " - " + title).c_str());
    status_message_ = emulator_.booting_through_bios() ? "ROM loaded at the BIOS reset vector"
                                                       : "ROM loaded with direct boot";
    status_is_error_ = false;
    framebuffer_dirty_ = true;
    fast_forward_locked_ = false;
    select_cheat(-1);
    cheat_error_.clear();
    if (!emulator_.cheat_file_message().empty()) {
        show_notice(emulator_.cheat_file_message(), true);
    } else if (!emulator_.cheats().empty()) {
        show_notice(std::to_string(emulator_.cheats().size()) + " cheat(s) loaded");
    }
}

void Application::load_bios(const std::filesystem::path& path) {
    std::string error;
    if (!emulator_.load_bios(path, error)) {
        status_message_ = std::move(error);
        status_is_error_ = true;
        show_notice(status_message_, true);
        return;
    }

    settings_.bios_path = path_to_utf8(path.lexically_normal());
    status_message_ = "BIOS loaded and validated";
    status_is_error_ = false;
    framebuffer_dirty_ = true;
}

void Application::close_rom() {
    emulator_.unload_rom();
    SDL_SetWindowTitle(window_, kWindowTitle);
    status_message_ = "ROM closed";
    status_is_error_ = false;
    framebuffer_dirty_ = true;
    fast_forward_locked_ = false;
    select_cheat(-1);
}

void Application::save_state_slot(const int slot) {
    if (!emulator_.has_rom()) {
        return;
    }
    std::string error;
    if (emulator_.save_state_slot(slot, error)) {
        show_notice("Saved state to slot " + std::to_string(slot));
    } else {
        show_notice("Could not save slot " + std::to_string(slot) + ": " + error, true);
    }
}

void Application::load_state_slot(const int slot) {
    if (!emulator_.has_rom()) {
        return;
    }
    std::string error;
    if (emulator_.load_state_slot(slot, error)) {
        framebuffer_dirty_ = true;
        show_notice("Loaded state from slot " + std::to_string(slot));
    } else {
        show_notice(error, true);
    }
}

void Application::toggle_pause() {
    if (emulator_.has_rom()) {
        emulator_.set_paused(!emulator_.is_paused());
    }
}

void Application::advance_frame() {
    if (!emulator_.has_rom()) {
        return;
    }
    if (!emulator_.is_paused()) {
        emulator_.set_paused(true);
        show_notice("Paused. Press N again to advance one frame.");
        return;
    }
    emulator_.advance_frame();
    framebuffer_dirty_ = true;
    // Frame advance is silent; drop the frame's audio rather than queueing it for later.
    audio_buffer_.clear();
    emulator_.take_audio_samples(audio_buffer_);
}

void Application::reset_game() {
    if (!emulator_.has_rom()) {
        return;
    }
    emulator_.reset();
    framebuffer_dirty_ = true;
    status_message_ =
        emulator_.booting_through_bios() ? "ROM reset through BIOS" : "ROM reset with direct boot";
    status_is_error_ = false;
    show_notice("Game reset");
}

void Application::add_rom_folder(const std::filesystem::path& folder) {
    const auto value = path_to_utf8(folder.lexically_normal());
    if (std::find(settings_.rom_folders.begin(), settings_.rom_folders.end(), value) ==
        settings_.rom_folders.end()) {
        settings_.rom_folders.push_back(value);
    }
    rescan_library();
    show_notice("Scanning " + path_to_utf8(folder.filename()) + " for ROMs");
}

void Application::rescan_library() {
    std::vector<std::filesystem::path> folders;
    for (const auto& folder : settings_.rom_folders) {
        folders.push_back(path_from_utf8(folder));
    }
    if (folders.empty()) {
        library_.stop();
        library_entries_.clear();
        return;
    }
    library_.scan(std::move(folders), settings_.scan_subfolders);
}

void Application::apply_rewind_settings() {
    emulator_.set_rewind_enabled(settings_.rewind_enabled,
                                 static_cast<std::size_t>(settings_.rewind_buffer_mib) * 1024U *
                                     1024U);
}

void Application::apply_bindings(const InputBindings& bindings) {
    input_.set_bindings(bindings);
    bindings.store(settings_);
}

void Application::apply_scale_filter() const noexcept {
    const auto mode = settings_.scale_filter == ScaleFilter::Nearest ? SDL_SCALEMODE_NEAREST
                                                                     : SDL_SCALEMODE_LINEAR;
    if (framebuffer_texture_) {
        SDL_SetTextureScaleMode(framebuffer_texture_, mode);
    }
    if (filtered_texture_) {
        SDL_SetTextureScaleMode(filtered_texture_, mode);
    }
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
        application.file_dialog_result_.selected_path = path_from_utf8(*file_list);
    }
}

} // namespace srgba::app
