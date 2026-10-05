#include "app/input.hpp"

#include "srgba/core/interrupts.hpp"

#include <algorithm>

namespace srgba::app {
namespace {

using core::Key;

struct KeyboardBinding {
    SDL_Scancode scancode;
    Key key;
};

constexpr std::array<KeyboardBinding, 12> kKeyboard{{
    {SDL_SCANCODE_X, Key::A},
    {SDL_SCANCODE_Z, Key::B},
    {SDL_SCANCODE_BACKSPACE, Key::Select},
    {SDL_SCANCODE_RSHIFT, Key::Select},
    {SDL_SCANCODE_RETURN, Key::Start},
    {SDL_SCANCODE_RIGHT, Key::Right},
    {SDL_SCANCODE_LEFT, Key::Left},
    {SDL_SCANCODE_UP, Key::Up},
    {SDL_SCANCODE_DOWN, Key::Down},
    {SDL_SCANCODE_S, Key::R},
    {SDL_SCANCODE_A, Key::L},
    {SDL_SCANCODE_KP_ENTER, Key::Start},
}};

struct GamepadBinding {
    SDL_GamepadButton button;
    Key key;
};

constexpr std::array<GamepadBinding, 10> kGamepad{{
    {SDL_GAMEPAD_BUTTON_SOUTH, Key::A},
    {SDL_GAMEPAD_BUTTON_EAST, Key::B},
    {SDL_GAMEPAD_BUTTON_BACK, Key::Select},
    {SDL_GAMEPAD_BUTTON_START, Key::Start},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Key::Right},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, Key::Left},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, Key::Up},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, Key::Down},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Key::R},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Key::L},
}};

constexpr Sint16 kStickThreshold = 16000;

constexpr std::array<InputBinding, 10> kDescriptions{{
    {"A", "X", "A / Cross (bottom face button)"},
    {"B", "Z", "B / Circle (right face button)"},
    {"L", "A", "Left bumper"},
    {"R", "S", "Right bumper"},
    {"Start", "Enter", "Start / Menu"},
    {"Select", "Backspace or Right Shift", "Back / View"},
    {"Up", "Up arrow", "D-pad or left stick"},
    {"Down", "Down arrow", "D-pad or left stick"},
    {"Left", "Left arrow", "D-pad or left stick"},
    {"Right", "Right arrow", "D-pad or left stick"},
}};

[[nodiscard]] constexpr std::uint16_t bit(const Key key) noexcept {
    return static_cast<std::uint16_t>(key);
}

} // namespace

InputMapper::~InputMapper() {
    close_all();
}

void InputMapper::handle_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (auto* gamepad = SDL_OpenGamepad(event.gdevice.which)) {
            gamepads_.push_back(gamepad);
        }
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        const auto removed =
            std::find_if(gamepads_.begin(), gamepads_.end(), [&](SDL_Gamepad* pad) {
                return SDL_GetGamepadID(pad) == event.gdevice.which;
            });
        if (removed != gamepads_.end()) {
            SDL_CloseGamepad(*removed);
            gamepads_.erase(removed);
        }
    }
}

void InputMapper::close_all() noexcept {
    for (auto* gamepad : gamepads_) {
        SDL_CloseGamepad(gamepad);
    }
    gamepads_.clear();
}

std::uint16_t InputMapper::pressed_keys(const bool keyboard_enabled) const noexcept {
    std::uint16_t pressed = 0;
    if (keyboard_enabled) {
        int key_count = 0;
        const bool* keyboard = SDL_GetKeyboardState(&key_count);
        for (const auto& binding : kKeyboard) {
            if (keyboard && static_cast<int>(binding.scancode) < key_count &&
                keyboard[binding.scancode]) {
                pressed |= bit(binding.key);
            }
        }
    }

    for (auto* gamepad : gamepads_) {
        for (const auto& binding : kGamepad) {
            if (SDL_GetGamepadButton(gamepad, binding.button)) {
                pressed |= bit(binding.key);
            }
        }
        const auto x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        const auto y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        if (x > kStickThreshold) {
            pressed |= bit(Key::Right);
        } else if (x < -kStickThreshold) {
            pressed |= bit(Key::Left);
        }
        if (y > kStickThreshold) {
            pressed |= bit(Key::Down);
        } else if (y < -kStickThreshold) {
            pressed |= bit(Key::Up);
        }
    }

    // A real D-pad cannot report opposite directions at once; some games misbehave if it does.
    if ((pressed & bit(Key::Left)) != 0U && (pressed & bit(Key::Right)) != 0U) {
        pressed = static_cast<std::uint16_t>(pressed & ~(bit(Key::Left) | bit(Key::Right)));
    }
    if ((pressed & bit(Key::Up)) != 0U && (pressed & bit(Key::Down)) != 0U) {
        pressed = static_cast<std::uint16_t>(pressed & ~(bit(Key::Up) | bit(Key::Down)));
    }
    return pressed;
}

std::size_t InputMapper::gamepad_count() const noexcept {
    return gamepads_.size();
}

const std::array<InputBinding, 10>& InputMapper::bindings() noexcept {
    return kDescriptions;
}

} // namespace srgba::app
