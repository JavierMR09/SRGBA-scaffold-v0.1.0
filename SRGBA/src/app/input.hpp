#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <vector>

namespace srgba::app {

struct InputBinding {
    const char* gba_button;
    const char* keyboard;
    const char* controller;
};

// Translates the host keyboard and any connected gamepads into a GBA keypad mask. Bindings are
// fixed in M3; user remapping arrives with the M6 input settings.
class InputMapper {
  public:
    InputMapper() = default;
    ~InputMapper();

    InputMapper(const InputMapper&) = delete;
    InputMapper& operator=(const InputMapper&) = delete;

    void handle_event(const SDL_Event& event);
    void close_all() noexcept;

    // Returns an active-high mask of srgba::core::Key bits.
    [[nodiscard]] std::uint16_t pressed_keys(bool keyboard_enabled) const noexcept;
    [[nodiscard]] std::size_t gamepad_count() const noexcept;

    [[nodiscard]] static const std::array<InputBinding, 10>& bindings() noexcept;

  private:
    std::vector<SDL_Gamepad*> gamepads_;
};

} // namespace srgba::app
