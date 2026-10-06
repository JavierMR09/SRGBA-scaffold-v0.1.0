// Settings, cheats, and about windows.
#include "app/application.hpp"

#include "app/paths.hpp"
#include "srgba/core/cheats.hpp"

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace srgba::app {
namespace {

#ifndef SRGBA_VERSION
#define SRGBA_VERSION "dev"
#endif

const ImVec4 kErrorColor{1.0F, 0.38F, 0.36F, 1.0F};
const ImVec4 kWarningColor{1.0F, 0.74F, 0.28F, 1.0F};

// Rows of the controls table, in the order players think about them (indices into the GBA
// KEYINPUT bit order used by InputBindings).
constexpr std::array<std::size_t, kGbaButtonCount> kControlRows{6, 7, 5, 4, 0, 1, 9, 8, 3, 2};

struct Hotkey {
    const char* keys;
    const char* action;
};

constexpr std::array<Hotkey, 12> kHotkeys{{
    {"Ctrl+O", "Open ROM"},
    {"Space", "Pause or resume"},
    {"N", "Advance one frame (pauses first)"},
    {"Tab (hold) / right trigger", "Fast forward"},
    {"` (hold) / left trigger", "Rewind"},
    {"F1 - F9", "Load state slot 1 - 9"},
    {"Shift+F1 - F9", "Save state slot 1 - 9"},
    {"Ctrl+R", "Reset the game"},
    {"M", "Mute or unmute audio"},
    {"F11", "Toggle fullscreen"},
    {"Drag and drop", "Open a ROM, or add a folder to the library"},
    {"Double-click", "Play a game from the library"},
}};

[[nodiscard]] bool begin_tab(const char* label, const bool select) {
    return ImGui::BeginTabItem(label, nullptr, select ? ImGuiTabItemFlags_SetSelected : 0);
}

// Dimmed explanatory text that wraps to the window width.
void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void Application::draw_settings_window() {
    ImGui::SetNextWindowSize(ImVec2(620.0F, 600.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &show_settings_)) {
        ImGui::End();
        return;
    }
    if (!show_settings_) {
        capture_kind_ = CaptureKind::None;
    }

    ImGui::PushItemWidth(ImGui::GetFontSize() * 16.0F);
    const auto requested = requested_settings_tab_;
    requested_settings_tab_.reset();
    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (begin_tab("Video", requested == SettingsTab::Video)) {
            draw_video_settings();
            ImGui::EndTabItem();
        }
        if (begin_tab("Emulation", requested == SettingsTab::Emulation)) {
            draw_emulation_settings();
            ImGui::EndTabItem();
        }
        if (begin_tab("Controls", requested == SettingsTab::Controls)) {
            draw_controls_settings();
            ImGui::EndTabItem();
        } else if (capture_kind_ != CaptureKind::None) {
            capture_kind_ = CaptureKind::None;
        }
        if (begin_tab("Audio", requested == SettingsTab::Audio)) {
            draw_audio_settings();
            ImGui::EndTabItem();
        }
        if (begin_tab("System", requested == SettingsTab::System)) {
            draw_system_settings();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::End();
}

void Application::draw_video_settings() {
    ImGui::SeparatorText("Scaling");
    ImGui::Checkbox("Integer scaling (sharpest picture)", &settings_.integer_scaling);
    int selected_filter = settings_.scale_filter == ScaleFilter::Nearest ? 0 : 1;
    if (ImGui::RadioButton("Nearest neighbor", &selected_filter, 0)) {
        settings_.scale_filter = ScaleFilter::Nearest;
        apply_scale_filter();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Smooth (linear)", &selected_filter, 1)) {
        settings_.scale_filter = ScaleFilter::Linear;
        apply_scale_filter();
    }

    ImGui::SeparatorText("Colors and screen");
    if (ImGui::Checkbox("GBA LCD colors", &settings_.color_correction)) {
        display_dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ImGui::SetItemTooltip("Games were drawn for the GBA's dark, washed-out screen. This recreates "
                          "its colors so bright palettes look as their artists intended.");

    constexpr std::array<const char*, 3> kFilterNames{"None", "LCD grid", "Scanlines"};
    int filter = static_cast<int>(settings_.lcd_filter);
    if (ImGui::Combo("Screen filter", &filter, kFilterNames.data(),
                     static_cast<int>(kFilterNames.size()))) {
        settings_.lcd_filter = static_cast<core::LcdFilter>(filter);
        display_dirty_ = true;
    }
    ImGui::BeginDisabled(settings_.lcd_filter == core::LcdFilter::None);
    if (ImGui::SliderInt("Filter strength", &settings_.filter_strength, 0, 100, "%d%%")) {
        display_dirty_ = true;
    }
    ImGui::EndDisabled();
    hint("Filters are drawn at your screen's resolution and look best with integer scaling.");
}

void Application::draw_emulation_settings() {
    ImGui::SeparatorText("Fast forward");
    constexpr std::array<int, 5> kSpeeds{2, 3, 4, 8, 0};
    constexpr std::array<const char*, 5> kSpeedNames{"2x", "3x", "4x", "8x", "As fast as possible"};
    const auto current = std::find(kSpeeds.begin(), kSpeeds.end(), settings_.fast_forward_speed);
    int speed_index = current == kSpeeds.end() ? 2 : static_cast<int>(current - kSpeeds.begin());
    if (ImGui::Combo("Speed", &speed_index, kSpeedNames.data(),
                     static_cast<int>(kSpeedNames.size()))) {
        settings_.fast_forward_speed = kSpeeds[static_cast<std::size_t>(speed_index)];
    }
    hint("Hold Tab (or a gamepad's right trigger), or toggle it from the Emulation menu. Sound "
         "is muted while fast-forwarding.");

    ImGui::SeparatorText("Rewind");
    if (ImGui::Checkbox("Enable rewind", &settings_.rewind_enabled)) {
        apply_rewind_settings();
    }
    ImGui::BeginDisabled(!settings_.rewind_enabled);
    if (ImGui::SliderInt("History budget", &settings_.rewind_buffer_mib, 16, 512, "%d MiB")) {
        apply_rewind_settings();
    }
    ImGui::EndDisabled();
    hint("Hold ` (the key left of 1) or a gamepad's left trigger to rewind.");
    if (emulator_.has_rom() && emulator_.rewind_enabled()) {
        ImGui::TextDisabled("Recorded: %.1f seconds using %.1f MiB",
                            static_cast<double>(emulator_.rewind_depth()) * 2.0 / 59.73,
                            static_cast<double>(emulator_.rewind_memory_used()) /
                                (1024.0 * 1024.0));
    }

    ImGui::SeparatorText("Save states");
    ImGui::TextWrapped("Nine slots per game, stored as .ss1 - .ss9 files beside the game's save. "
                       "Shift+F1 - F9 saves and F1 - F9 loads; the States menu shows when each "
                       "slot was saved.");
}

void Application::draw_binding_cell(const std::size_t button, const CaptureKind kind,
                                    const std::size_t slot) {
    const auto& bindings = input_.bindings();
    const bool capturing =
        capture_kind_ == kind && capture_button_ == button && capture_slot_ == slot;
    std::string label;
    if (capturing) {
        label = kind == CaptureKind::Keyboard ? "Press a key..." : "Press a button...";
    } else if (kind == CaptureKind::Keyboard) {
        label = InputMapper::key_label(bindings.keyboard[button][slot]);
    } else {
        label = InputMapper::gamepad_label(bindings.gamepad[button][slot]);
    }
    if (label.empty()) {
        label = "-";
    }

    ImGui::PushID(static_cast<int>(button * 16U + slot * 4U + static_cast<std::size_t>(kind)));
    if (capturing) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.38F, 0.10F, 1.0F));
    }
    if (ImGui::Button(label.c_str(), ImVec2(-1.0F, 0.0F))) {
        capture_kind_ = capturing ? CaptureKind::None : kind;
        capture_button_ = button;
        capture_slot_ = slot;
    }
    if (capturing) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        auto updated = bindings;
        if (kind == CaptureKind::Keyboard) {
            updated.keyboard[button][slot] = SDL_SCANCODE_UNKNOWN;
        } else {
            updated.gamepad[button][slot] = SDL_GAMEPAD_BUTTON_INVALID;
        }
        apply_bindings(updated);
        capture_kind_ = CaptureKind::None;
    }
    ImGui::SetItemTooltip("Click, then press the new %s. Right-click to clear.",
                          kind == CaptureKind::Keyboard ? "key (Esc cancels)" : "gamepad button");
    ImGui::PopID();
}

void Application::draw_controls_settings() {
    ImGui::TextDisabled("Gamepads connected: %zu. The left stick always works as the D-pad.",
                        input_.gamepad_count());
    constexpr auto table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("Bindings", 5, table_flags)) {
        ImGui::TableSetupColumn("GBA", ImGuiTableColumnFlags_WidthStretch, 0.7F);
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Alternate key", ImGuiTableColumnFlags_WidthStretch, 1.0F);
        ImGui::TableSetupColumn("Gamepad", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableSetupColumn("Alternate", ImGuiTableColumnFlags_WidthStretch, 1.2F);
        ImGui::TableHeadersRow();
        for (const auto button : kControlRows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(InputMapper::button_name(button));
            ImGui::TableSetColumnIndex(1);
            draw_binding_cell(button, CaptureKind::Keyboard, 0);
            ImGui::TableSetColumnIndex(2);
            draw_binding_cell(button, CaptureKind::Keyboard, 1);
            ImGui::TableSetColumnIndex(3);
            draw_binding_cell(button, CaptureKind::Gamepad, 0);
            ImGui::TableSetColumnIndex(4);
            draw_binding_cell(button, CaptureKind::Gamepad, 1);
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Reset to defaults")) {
        apply_bindings(InputBindings::defaults());
        capture_kind_ = CaptureKind::None;
    }

    ImGui::SeparatorText("Hotkeys");
    if (ImGui::BeginTable("Hotkeys", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const auto& hotkey : kHotkeys) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(hotkey.keys);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", hotkey.action);
        }
        ImGui::EndTable();
    }
}

void Application::draw_audio_settings() {
    if (!audio_stream_) {
        ImGui::TextDisabled("No audio output device is available.");
    }
    if (ImGui::SliderInt("Volume", &settings_.audio_volume, 0, 100, "%d%%")) {
        apply_audio_gain();
    }
    if (ImGui::Checkbox("Mute", &settings_.audio_muted)) {
        apply_audio_gain();
    }
}

void Application::draw_system_settings() {
    ImGui::SeparatorText("Boot");
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
        const auto bios_path = path_from_utf8(settings_.bios_path);
        ImGui::TextDisabled("BIOS: %s (CRC32 %08X)", path_to_utf8(bios_path.filename()).c_str(),
                            static_cast<unsigned>(emulator_.bus().bios_crc32()));
    } else {
        ImGui::TextDisabled("No BIOS loaded; SRGBA uses direct boot and its built-in BIOS.");
    }
    if (ImGui::Button("Choose BIOS...")) {
        request_open_bios();
    }

    ImGui::SeparatorText("Saves");
    if (emulator_.has_rom()) {
        const auto type = core::save_type_name(emulator_.save_type());
        ImGui::TextDisabled("Save chip: %.*s", static_cast<int>(type.size()), type.data());
        if (!emulator_.save_path().empty()) {
            ImGui::TextWrapped("Save file: %s", path_to_utf8(emulator_.save_path()).c_str());
        }
    } else {
        ImGui::TextDisabled("Battery saves, save states and cheats are stored next to each ROM.");
    }
}

void Application::select_cheat(const int index) {
    selected_cheat_ = index;
    const auto& cheats = emulator_.cheats();
    if (index < 0 || static_cast<std::size_t>(index) >= cheats.size()) {
        selected_cheat_ = -1;
        cheat_description_.clear();
        cheat_code_.clear();
        cheat_format_index_ = 0;
        return;
    }
    const auto& cheat = cheats.cheat(static_cast<std::size_t>(index));
    cheat_description_ = cheat.description;
    cheat_code_ = cheat.code;
    cheat_format_index_ = static_cast<int>(cheat.format);
    cheat_error_ = cheats.problem(static_cast<std::size_t>(index));
}

void Application::persist_cheats() {
    std::string error;
    if (!emulator_.save_cheats(error)) {
        show_notice("Could not save cheats: " + error, true);
    }
}

void Application::draw_cheats_window() {
    ImGui::SetNextWindowSize(ImVec2(640.0F, 560.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cheats", &show_cheats_)) {
        ImGui::End();
        return;
    }
    if (!emulator_.has_rom()) {
        ImGui::TextWrapped("Load a game to manage its cheats. Each game keeps its own list in a "
                           ".cht file beside its save.");
        ImGui::End();
        return;
    }

    auto& cheats = emulator_.cheats();
    if (!emulator_.cheat_file_message().empty()) {
        ImGui::TextColored(kWarningColor, "%s", emulator_.cheat_file_message().c_str());
    }

    const float list_height = std::max(140.0F, ImGui::GetContentRegionAvail().y * 0.42F);
    constexpr auto table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                 ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("CheatList", 3, table_flags, ImVec2(0.0F, list_height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 32.0F);
        ImGui::TableSetupColumn("Cheat", ImGuiTableColumnFlags_WidthStretch, 3.0F);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch, 1.6F);
        ImGui::TableHeadersRow();
        std::optional<int> clicked;
        for (std::size_t index = 0; index < cheats.size(); ++index) {
            const auto& cheat = cheats.cheat(index);
            const auto& problem = cheats.problem(index);
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(index));
            ImGui::TableSetColumnIndex(0);
            bool enabled = cheat.enabled;
            ImGui::BeginDisabled(!problem.empty());
            if (ImGui::Checkbox("##enabled", &enabled)) {
                cheats.set_enabled(index, enabled);
                persist_cheats();
            }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(1);
            const auto name =
                cheat.description.empty() ? std::string("(unnamed cheat)") : cheat.description;
            if (ImGui::Selectable(name.c_str(), selected_cheat_ == static_cast<int>(index),
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                clicked = static_cast<int>(index);
            }
            if (!problem.empty()) {
                ImGui::SetItemTooltip("%s", problem.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            if (!problem.empty()) {
                ImGui::TextColored(kErrorColor, "Cannot decode");
            } else {
                const auto format = core::cheat_format_name(cheats.decoded_format(index));
                ImGui::TextDisabled("%.*s", static_cast<int>(format.size()), format.data());
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (clicked) {
            select_cheat(*clicked == selected_cheat_ ? -1 : *clicked);
        }
    }
    if (cheats.empty()) {
        ImGui::TextDisabled("No cheats yet. Paste a code below and choose Add.");
    }

    ImGui::SeparatorText(selected_cheat_ >= 0 ? "Edit cheat" : "New cheat");
    ImGui::InputTextWithHint("Description", "e.g. Infinite health", &cheat_description_);
    std::array<const char*, std::size(core::kCheatFormats)> format_names{};
    std::array<std::string, std::size(core::kCheatFormats)> format_storage{};
    for (std::size_t index = 0; index < format_names.size(); ++index) {
        format_storage[index] = std::string(core::cheat_format_name(core::kCheatFormats[index]));
        format_names[index] = format_storage[index].c_str();
    }
    ImGui::Combo("Format", &cheat_format_index_, format_names.data(),
                 static_cast<int>(format_names.size()));
    ImGui::InputTextMultiline("##code", &cheat_code_, ImVec2(-1.0F, 110.0F));
    hint("One code per line: 02000000:63 (raw), 82000000 0063 (CodeBreaker), or 8 + 8 digit "
         "GameShark / Action Replay codes. Automatic picks the format for you.");

    const auto make_cheat = [&] {
        core::Cheat cheat;
        cheat.description = cheat_description_;
        cheat.code = cheat_code_;
        cheat.format = core::kCheatFormats[static_cast<std::size_t>(
            std::clamp(cheat_format_index_, 0, static_cast<int>(format_names.size()) - 1))];
        return cheat;
    };

    if (ImGui::Button(selected_cheat_ >= 0 ? "Add as new" : "Add cheat")) {
        if (cheats.add(make_cheat(), cheat_error_)) {
            persist_cheats();
            select_cheat(-1);
            show_notice("Cheat added");
        }
    }
    if (selected_cheat_ >= 0) {
        ImGui::SameLine();
        if (ImGui::Button("Save changes")) {
            auto cheat = make_cheat();
            cheat.enabled = cheats.cheat(static_cast<std::size_t>(selected_cheat_)).enabled ||
                            !cheats.problem(static_cast<std::size_t>(selected_cheat_)).empty();
            if (cheats.replace(static_cast<std::size_t>(selected_cheat_), std::move(cheat),
                               cheat_error_)) {
                persist_cheats();
                select_cheat(selected_cheat_);
                show_notice("Cheat updated");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            cheats.remove(static_cast<std::size_t>(selected_cheat_));
            persist_cheats();
            select_cheat(-1);
            cheat_error_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("New")) {
            select_cheat(-1);
            cheat_error_.clear();
        }
    }
    if (!cheat_error_.empty()) {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(kErrorColor, "%s", cheat_error_.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Enabled cheats run every frame. Saved in %s",
                        path_to_utf8(emulator_.cheat_file_path().filename()).c_str());
    ImGui::End();
}

void Application::draw_about_window() {
    ImGui::SetNextWindowSize(ImVec2(460.0F, 250.0F), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("About SRGBA", &show_about_)) {
        ImGui::End();
        return;
    }

    ImGui::SetWindowFontScale(1.5F);
    ImGui::Text("SRGBA %s", SRGBA_VERSION);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::TextWrapped(
        "A clean-room Game Boy Advance emulator project built with C++20, SDL3, and Dear ImGui.");
    ImGui::Spacing();
    hint("Current status: M6 emulator features (save states, rewind, cheats, remapping, and the "
         "ROM library)");
    ImGui::TextDisabled("License: MIT");
    ImGui::Spacing();
    ImGui::TextWrapped("SRGBA does not include commercial ROMs or Nintendo BIOS files.");
    ImGui::End();
}

} // namespace srgba::app
