#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace srgba::app {

struct Settings;

inline constexpr std::size_t kGbaButtonCount = 10;
inline constexpr std::size_t kBindingsPerButton = 2;

// Host inputs assigned to each GBA button, in InputMapper::button_name() order. Unused slots hold
// SDL_SCANCODE_UNKNOWN or SDL_GAMEPAD_BUTTON_INVALID.
struct InputBindings {
    std::array<std::array<SDL_Scancode, kBindingsPerButton>, kGbaButtonCount> keyboard{};
    std::array<std::array<SDL_GamepadButton, kBindingsPerButton>, kGbaButtonCount> gamepad{};

    [[nodiscard]] static InputBindings defaults() noexcept;
    // Reads the bindings saved in `settings`, falling back to the defaults for buttons that have
    // none saved.
    [[nodiscard]] static InputBindings from_settings(const Settings& settings);
    void store(Settings& settings) const;
};

// Translates the host keyboard and any connected gamepads into a GBA keypad mask using the
// user's bindings. The left stick always doubles as the D-pad.
class InputMapper {
  public:
    InputMapper() = default;
    ~InputMapper();

    InputMapper(const InputMapper&) = delete;
    InputMapper& operator=(const InputMapper&) = delete;

    void handle_event(const SDL_Event& event);
    void close_all() noexcept;

    void set_bindings(const InputBindings& bindings) noexcept;
    [[nodiscard]] const InputBindings& bindings() const noexcept;

    // Returns an active-high mask of srgba::core::Key bits.
    [[nodiscard]] std::uint16_t pressed_keys(bool keyboard_enabled) const noexcept;
    [[nodiscard]] std::size_t gamepad_count() const noexcept;
    // True while any connected gamepad holds the trigger past its halfway point.
    [[nodiscard]] bool trigger_held(SDL_GamepadAxis trigger) const noexcept;

    [[nodiscard]] static const char* button_name(std::size_t index) noexcept;
    [[nodiscard]] static std::string key_label(SDL_Scancode scancode);
    [[nodiscard]] static std::string gamepad_label(SDL_GamepadButton button);

  private:
    std::vector<SDL_Gamepad*> gamepads_;
    InputBindings bindings_{InputBindings::defaults()};
};

} // namespace srgba::app
