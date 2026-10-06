#include "app/input.hpp"

#include "app/settings.hpp"
#include "srgba/core/interrupts.hpp"

#include <algorithm>

namespace srgba::app {
namespace {

using core::Key;

// Index i is GBA KEYINPUT bit i.
constexpr std::array<const char*, kGbaButtonCount> kButtonNames{
    "A", "B", "Select", "Start", "Right", "Left", "Up", "Down", "R", "L",
};

constexpr Sint16 kStickThreshold = 16000;
constexpr Sint16 kTriggerThreshold = 16000;

[[nodiscard]] constexpr std::uint16_t bit(const Key key) noexcept {
    return static_cast<std::uint16_t>(key);
}

[[nodiscard]] constexpr std::uint16_t button_bit(const std::size_t index) noexcept {
    return static_cast<std::uint16_t>(1U << index);
}

} // namespace

InputBindings InputBindings::defaults() noexcept {
    InputBindings bindings;
    for (auto& keys : bindings.keyboard) {
        keys.fill(SDL_SCANCODE_UNKNOWN);
    }
    for (auto& buttons : bindings.gamepad) {
        buttons.fill(SDL_GAMEPAD_BUTTON_INVALID);
    }
    const auto set = [&](const std::size_t index, const SDL_Scancode primary,
                         const SDL_Scancode secondary, const SDL_GamepadButton button) {
        bindings.keyboard[index] = {primary, secondary};
        bindings.gamepad[index][0] = button;
    };
    set(0, SDL_SCANCODE_X, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_SOUTH);
    set(1, SDL_SCANCODE_Z, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_EAST);
    set(2, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_RSHIFT, SDL_GAMEPAD_BUTTON_BACK);
    set(3, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER, SDL_GAMEPAD_BUTTON_START);
    set(4, SDL_SCANCODE_RIGHT, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    set(5, SDL_SCANCODE_LEFT, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    set(6, SDL_SCANCODE_UP, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_DPAD_UP);
    set(7, SDL_SCANCODE_DOWN, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    set(8, SDL_SCANCODE_S, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    set(9, SDL_SCANCODE_A, SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    return bindings;
}

InputBindings InputBindings::from_settings(const Settings& settings) {
    auto bindings = defaults();
    for (std::size_t index = 0; index < kGbaButtonCount; ++index) {
        const std::string name = kButtonNames[index];
        if (const auto saved = settings.keyboard_bindings.find(name);
            saved != settings.keyboard_bindings.end()) {
            auto& keys = bindings.keyboard[index];
            keys.fill(SDL_SCANCODE_UNKNOWN);
            for (std::size_t slot = 0; slot < std::min(saved->second.size(), keys.size()); ++slot) {
                keys[slot] = SDL_GetScancodeFromName(saved->second[slot].c_str());
            }
        }
        if (const auto saved = settings.gamepad_bindings.find(name);
            saved != settings.gamepad_bindings.end()) {
            auto& buttons = bindings.gamepad[index];
            buttons.fill(SDL_GAMEPAD_BUTTON_INVALID);
            for (std::size_t slot = 0; slot < std::min(saved->second.size(), buttons.size());
                 ++slot) {
                buttons[slot] = SDL_GetGamepadButtonFromString(saved->second[slot].c_str());
            }
        }
    }
    return bindings;
}

void InputBindings::store(Settings& settings) const {
    settings.keyboard_bindings.clear();
    settings.gamepad_bindings.clear();
    for (std::size_t index = 0; index < kGbaButtonCount; ++index) {
        auto& keys = settings.keyboard_bindings[kButtonNames[index]];
        for (const auto scancode : keyboard[index]) {
            const char* name =
                scancode == SDL_SCANCODE_UNKNOWN ? "" : SDL_GetScancodeName(scancode);
            keys.emplace_back(name != nullptr ? name : "");
        }
        auto& buttons = settings.gamepad_bindings[kButtonNames[index]];
        for (const auto button : gamepad[index]) {
            const char* name =
                button == SDL_GAMEPAD_BUTTON_INVALID ? "" : SDL_GetGamepadStringForButton(button);
            buttons.emplace_back(name != nullptr ? name : "");
        }
    }
}

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

void InputMapper::set_bindings(const InputBindings& bindings) noexcept {
    bindings_ = bindings;
}

const InputBindings& InputMapper::bindings() const noexcept {
    return bindings_;
}

std::uint16_t InputMapper::pressed_keys(const bool keyboard_enabled) const noexcept {
    std::uint16_t pressed = 0;
    if (keyboard_enabled) {
        int key_count = 0;
        const bool* keyboard = SDL_GetKeyboardState(&key_count);
        for (std::size_t index = 0; index < kGbaButtonCount && keyboard; ++index) {
            for (const auto scancode : bindings_.keyboard[index]) {
                if (scancode != SDL_SCANCODE_UNKNOWN && static_cast<int>(scancode) < key_count &&
                    keyboard[scancode]) {
                    pressed |= button_bit(index);
                }
            }
        }
    }

    for (auto* gamepad : gamepads_) {
        for (std::size_t index = 0; index < kGbaButtonCount; ++index) {
            for (const auto button : bindings_.gamepad[index]) {
                if (button != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(gamepad, button)) {
                    pressed |= button_bit(index);
                }
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

bool InputMapper::trigger_held(const SDL_GamepadAxis trigger) const noexcept {
    return std::any_of(gamepads_.begin(), gamepads_.end(), [&](SDL_Gamepad* gamepad) {
        return SDL_GetGamepadAxis(gamepad, trigger) > kTriggerThreshold;
    });
}

const char* InputMapper::button_name(const std::size_t index) noexcept {
    return index < kButtonNames.size() ? kButtonNames[index] : "?";
}

std::string InputMapper::key_label(const SDL_Scancode scancode) {
    if (scancode == SDL_SCANCODE_UNKNOWN) {
        return {};
    }
    // Show the key printed on the user's keyboard layout when SDL knows it.
    const auto keycode = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
    const char* name = keycode != SDLK_UNKNOWN ? SDL_GetKeyName(keycode) : nullptr;
    if (name == nullptr || *name == '\0') {
        name = SDL_GetScancodeName(scancode);
    }
    return name != nullptr ? name : "?";
}

std::string InputMapper::gamepad_label(const SDL_GamepadButton button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_INVALID:
        return {};
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return "A / Cross";
    case SDL_GAMEPAD_BUTTON_EAST:
        return "B / Circle";
    case SDL_GAMEPAD_BUTTON_WEST:
        return "X / Square";
    case SDL_GAMEPAD_BUTTON_NORTH:
        return "Y / Triangle";
    case SDL_GAMEPAD_BUTTON_BACK:
        return "Back / View";
    case SDL_GAMEPAD_BUTTON_START:
        return "Start / Menu";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return "Left bumper";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return "Right bumper";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return "Left stick press";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return "Right stick press";
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return "D-pad up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return "D-pad down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return "D-pad left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return "D-pad right";
    default:
        break;
    }
    const char* name = SDL_GetGamepadStringForButton(button);
    return name != nullptr ? name : "?";
}

} // namespace srgba::app
