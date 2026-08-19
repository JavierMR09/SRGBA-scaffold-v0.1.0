#include "srgba/core/emulator.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace srgba::core {
namespace {

constexpr std::uint32_t kMasterCyclesPerFrame = 280896U;

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
        bus_.set_game_pak(cartridge_->bytes());
        reset_machine();
        error_message.clear();
        render_scaffold_frame();
        return true;
    } catch (const std::exception& exception) {
        error_message = exception.what();
        return false;
    }
}

bool Emulator::load_bios(const std::filesystem::path& path, std::string& error_message) {
    if (!bus_.load_bios(path, error_message)) {
        return false;
    }
    if (cartridge_ && boot_mode_ == BootMode::Bios) {
        reset_machine();
    }
    return true;
}

void Emulator::unload_rom() noexcept {
    bus_.clear_game_pak();
    cartridge_.reset();
    bus_.reset();
    cpu_.reset();
    state_ = RunState::Empty;
    frame_counter_ = 0;
    instruction_counter_ = 0;
    cycle_counter_ = 0;
    render_idle_frame();
}

void Emulator::unload_bios() noexcept {
    const bool reset_active_machine = cartridge_.has_value() && boot_mode_ == BootMode::Bios;
    bus_.unload_bios();
    if (reset_active_machine) {
        reset_machine();
    }
}

void Emulator::reset() noexcept {
    if (!cartridge_) {
        return;
    }
    reset_machine();
    render_scaffold_frame();
}

void Emulator::run_frame() noexcept {
    if (state_ != RunState::Running) {
        return;
    }

    std::uint32_t elapsed_cycles = 0;
    while (state_ == RunState::Running && elapsed_cycles < kMasterCyclesPerFrame) {
        const auto result = step_instruction();
        if (!result) {
            break;
        }
        elapsed_cycles += std::max(result->cycles, 1U);
    }
    ++frame_counter_;
    render_scaffold_frame();
}

std::optional<ExecutionResult> Emulator::step_instruction() noexcept {
    if (state_ != RunState::Running || !cartridge_) {
        return std::nullopt;
    }

    auto result = cpu_.step(bus_);
    ++instruction_counter_;
    cycle_counter_ += result.cycles;
    if (result.status == ExecutionStatus::UnsupportedInstruction ||
        result.status == ExecutionStatus::WrongInstructionSet) {
        state_ = RunState::Paused;
    }
    return result;
}

void Emulator::set_paused(const bool paused) noexcept {
    if (!cartridge_) {
        return;
    }
    state_ = paused ? RunState::Paused : RunState::Running;
}

void Emulator::set_boot_mode(const BootMode mode) noexcept {
    if (boot_mode_ == mode) {
        return;
    }
    boot_mode_ = mode;
    if (cartridge_) {
        reset_machine();
        render_scaffold_frame();
    }
}

bool Emulator::has_rom() const noexcept {
    return cartridge_.has_value();
}

bool Emulator::has_bios() const noexcept {
    return bus_.has_bios();
}

bool Emulator::is_paused() const noexcept {
    return state_ == RunState::Paused;
}

bool Emulator::booting_through_bios() const noexcept {
    return boot_mode_ == BootMode::Bios && bus_.has_bios();
}

RunState Emulator::state() const noexcept {
    return state_;
}

BootMode Emulator::boot_mode() const noexcept {
    return boot_mode_;
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

std::uint64_t Emulator::instruction_counter() const noexcept {
    return instruction_counter_;
}

std::uint64_t Emulator::cycle_counter() const noexcept {
    return cycle_counter_;
}

const Framebuffer& Emulator::framebuffer() const noexcept {
    return framebuffer_;
}

const Arm7Tdmi& Emulator::cpu() const noexcept {
    return cpu_;
}

GbaBus& Emulator::bus() noexcept {
    return bus_;
}

const GbaBus& Emulator::bus() const noexcept {
    return bus_;
}

void Emulator::reset_machine() noexcept {
    bus_.reset();
    cpu_.reset();
    if (booting_through_bios()) {
        cpu_.set_program_counter(GbaBus::kBiosStart);
    } else {
        initialize_direct_boot();
    }
    state_ = cartridge_ ? RunState::Running : RunState::Empty;
    frame_counter_ = 0;
    instruction_counter_ = 0;
    cycle_counter_ = 0;
}

void Emulator::initialize_direct_boot() noexcept {
    bus_.initialize_post_bios();

    cpu_.cpsr().set_mode(ProcessorMode::Supervisor);
    cpu_.set_register(Arm7Tdmi::kStackPointer, 0x03007FE0U);
    cpu_.cpsr().set_mode(ProcessorMode::Irq);
    cpu_.set_register(Arm7Tdmi::kStackPointer, 0x03007FA0U);
    cpu_.cpsr().set_mode(ProcessorMode::System);
    cpu_.set_register(Arm7Tdmi::kStackPointer, 0x03007F00U);
    cpu_.cpsr().set_instruction_set(InstructionSet::Arm);
    cpu_.cpsr().set_irq_disabled(false);
    cpu_.cpsr().set_fiq_disabled(false);
    cpu_.set_program_counter(GbaBus::kGamePakStart);
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
