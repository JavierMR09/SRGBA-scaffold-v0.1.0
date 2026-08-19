#include "srgba/core/emulator.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace srgba::core {
namespace {

[[nodiscard]] constexpr std::uint8_t as_byte(const std::size_t value) noexcept {
    return static_cast<std::uint8_t>(value & 0xFFU);
}

} // namespace

Emulator::Emulator() {
    render_idle_frame();
}

bool Emulator::load_rom(const std::filesystem::path& path, std::string& error_message) {
    try {
        auto cartridge = Cartridge::load(path);
        cartridge_ = std::move(cartridge);
        cpu_.reset();
        state_ = RunState::Running;
        frame_counter_ = 0;
        error_message.clear();
        render_scaffold_frame();
        return true;
    } catch (const std::exception& exception) {
        error_message = exception.what();
        return false;
    }
}

void Emulator::unload_rom() noexcept {
    cartridge_.reset();
    cpu_.reset();
    state_ = RunState::Empty;
    frame_counter_ = 0;
    render_idle_frame();
}

void Emulator::reset() noexcept {
    if (!cartridge_) {
        return;
    }
    cpu_.reset();
    state_ = RunState::Running;
    frame_counter_ = 0;
    render_scaffold_frame();
}

void Emulator::run_frame() noexcept {
    if (state_ != RunState::Running) {
        return;
    }
    ++frame_counter_;
    render_scaffold_frame();
}

void Emulator::set_paused(const bool paused) noexcept {
    if (!cartridge_) {
        return;
    }
    state_ = paused ? RunState::Paused : RunState::Running;
}

bool Emulator::has_rom() const noexcept {
    return cartridge_.has_value();
}

bool Emulator::is_paused() const noexcept {
    return state_ == RunState::Paused;
}

RunState Emulator::state() const noexcept {
    return state_;
}

const RomHeader* Emulator::rom_header() const noexcept {
    return cartridge_ ? &cartridge_->header() : nullptr;
}

const std::filesystem::path* Emulator::rom_path() const noexcept {
    return cartridge_ ? &cartridge_->path() : nullptr;
}

std::size_t Emulator::rom_size() const noexcept {
    return cartridge_ ? cartridge_->size() : 0;
}

std::uint64_t Emulator::frame_counter() const noexcept {
    return frame_counter_;
}

const Framebuffer& Emulator::framebuffer() const noexcept {
    return framebuffer_;
}

const Arm7Tdmi& Emulator::cpu() const noexcept {
    return cpu_;
}

void Emulator::render_idle_frame() noexcept {
    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            const auto index = y * kScreenWidth + x;
            const auto glow = static_cast<std::uint8_t>((x * 22U) / kScreenWidth);
            framebuffer_[index] = Rgba8{
                static_cast<std::uint8_t>(10U + glow / 3U),
                static_cast<std::uint8_t>(13U + glow / 2U),
                static_cast<std::uint8_t>(22U + glow),
                255,
            };
        }
    }
}

void Emulator::render_scaffold_frame() noexcept {
    constexpr std::size_t block_size = 16;
    const auto animation_offset = static_cast<std::size_t>((frame_counter_ / 4U) % block_size);

    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        for (std::size_t x = 0; x < kScreenWidth; ++x) {
            const auto index = y * kScreenWidth + x;
            const bool alternate =
                (((x + animation_offset) / block_size) + (y / block_size)) % 2U == 0U;
            const auto horizontal = as_byte((x * 80U) / kScreenWidth);
            const auto vertical = as_byte((y * 55U) / kScreenHeight);

            if (alternate) {
                framebuffer_[index] = Rgba8{
                    static_cast<std::uint8_t>(32U + horizontal),
                    static_cast<std::uint8_t>(74U + vertical),
                    static_cast<std::uint8_t>(138U + horizontal / 2U),
                    255,
                };
            } else {
                framebuffer_[index] = Rgba8{
                    static_cast<std::uint8_t>(18U + vertical / 2U),
                    static_cast<std::uint8_t>(32U + horizontal / 3U),
                    static_cast<std::uint8_t>(72U + vertical),
                    255,
                };
            }
        }
    }

    // A bright frame makes it obvious that this is scaffold output, not emulated video.
    constexpr Rgba8 border{86, 215, 255, 255};
    for (std::size_t x = 0; x < kScreenWidth; ++x) {
        framebuffer_[x] = border;
        framebuffer_[(kScreenHeight - 1U) * kScreenWidth + x] = border;
    }
    for (std::size_t y = 0; y < kScreenHeight; ++y) {
        framebuffer_[y * kScreenWidth] = border;
        framebuffer_[y * kScreenWidth + (kScreenWidth - 1U)] = border;
    }
}

} // namespace srgba::core
