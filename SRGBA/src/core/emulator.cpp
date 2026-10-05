#include "srgba/core/emulator.hpp"

#include "srgba/core/hle_bios.hpp"
#include "srgba/core/system_bios.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace srgba::core {
namespace {

constexpr std::uint64_t kMasterCyclesPerFrame = Ppu::kCyclesPerFrame;
static_assert(kMasterCyclesPerFrame == 280896U);

} // namespace

Emulator::Emulator() {
    bus_.attach_scheduler(scheduler_);
    render_idle_frame();
}

bool Emulator::load_rom(const std::filesystem::path& path, std::string& error_message) {
    try {
        auto cartridge = Cartridge::load(path);
        cartridge_ = std::move(cartridge);
        bus_.set_game_pak(cartridge_->bytes());
        reset_machine();
        error_message.clear();
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
    scheduler_.reset();
    state_ = RunState::Empty;
    frame_counter_ = 0;
    instruction_counter_ = 0;
    halted_ = false;
    fault_.reset();
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
}

void Emulator::run_frame() noexcept {
    if (state_ != RunState::Running) {
        return;
    }

    // Frames are aligned to the PPU: line 0 starts at every multiple of 280,896 cycles.
    const auto frame_end = (scheduler_.now() / kMasterCyclesPerFrame + 1U) * kMasterCyclesPerFrame;
    while (state_ == RunState::Running && scheduler_.now() < frame_end) {
        if (!step_instruction()) {
            break;
        }
    }
    if (scheduler_.now() >= frame_end) {
        ++frame_counter_;
    }
}

std::optional<ExecutionResult> Emulator::step_instruction() noexcept {
    if (state_ != RunState::Running || !cartridge_) {
        return std::nullopt;
    }

    ExecutionResult result{};
    if (halted_) {
        result.cycles = static_cast<std::uint32_t>(scheduler_.skip_to_next_event());
    } else if (bus_.using_builtin_bios() &&
               cpu_.program_counter() == builtin_bios::kSoftwareInterruptVector &&
               cpu_.cpsr().mode() == ProcessorMode::Supervisor &&
               cpu_.cpsr().instruction_set() == InstructionSet::Arm) {
        result.cycles = HleBios::handle_swi(cpu_, bus_);
        result.pipeline_flushed = true;
        ++instruction_counter_;
        scheduler_.advance(result.cycles);
    } else {
        result = cpu_.step(bus_);
        ++instruction_counter_;
        if (result.status == ExecutionStatus::UnsupportedInstruction ||
            result.status == ExecutionStatus::WrongInstructionSet) {
            record_fault();
            state_ = RunState::Paused;
            return result;
        }
        // Immediate DMA started by this instruction runs before the next one.
        scheduler_.advance(std::max(result.cycles, 1U) + bus_.take_dma_cycles());
    }

    if (bus_.take_halt_request()) {
        halted_ = true;
    }
    process_events();
    service_interrupts();
    return result;
}

void Emulator::set_pressed_keys(const std::uint16_t pressed) noexcept {
    pressed_keys_ = static_cast<std::uint16_t>(pressed & kKeyMask);
    bus_.set_pressed_keys(pressed_keys_);
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

bool Emulator::is_halted() const noexcept {
    return halted_;
}

const std::optional<CpuFault>& Emulator::fault() const noexcept {
    return fault_;
}

std::uint16_t Emulator::pressed_keys() const noexcept {
    return pressed_keys_;
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
    return scheduler_.now();
}

const Framebuffer& Emulator::framebuffer() const noexcept {
    return framebuffer_;
}

const Arm7Tdmi& Emulator::cpu() const noexcept {
    return cpu_;
}

const Ppu& Emulator::ppu() const noexcept {
    return ppu_;
}

const Scheduler& Emulator::scheduler() const noexcept {
    return scheduler_;
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
    bus_.set_pressed_keys(pressed_keys_);
    scheduler_.reset();
    ppu_.reset(bus_, scheduler_);
    halted_ = false;
    fault_.reset();
    clear_framebuffer();
    state_ = cartridge_ ? RunState::Running : RunState::Empty;
    frame_counter_ = 0;
    instruction_counter_ = 0;
}

void Emulator::process_events() noexcept {
    for (;;) {
        while (const auto event = scheduler_.pop_due()) {
            switch (event->type) {
            case EventType::HBlankStart:
                ppu_.on_hblank_start(bus_, scheduler_, event->timestamp, framebuffer_);
                break;
            case EventType::ScanlineEnd:
                ppu_.on_scanline_end(bus_, scheduler_, event->timestamp);
                break;
            case EventType::Timer0Overflow:
            case EventType::Timer1Overflow:
            case EventType::Timer2Overflow:
            case EventType::Timer3Overflow:
                static_cast<void>(
                    bus_.on_timer_overflow(static_cast<std::size_t>(event->type) -
                                               static_cast<std::size_t>(EventType::Timer0Overflow),
                                           event->timestamp));
                break;
            case EventType::Count:
                break;
            }
        }
        // DMA transfers stall the CPU; time spent in them can make further events due.
        const auto stall = bus_.take_dma_cycles();
        if (stall == 0U) {
            return;
        }
        scheduler_.advance(stall);
    }
}

void Emulator::service_interrupts() noexcept {
    if (!bus_.interrupt_pending()) {
        return;
    }
    // Any enabled request wakes the CPU from HALT, even when IME or CPSR.I masks the exception.
    halted_ = false;
    if (bus_.interrupt_master_enable() && cpu_.try_take_irq()) {
        // Exception entry refills the pipeline (2S + 1N).
        scheduler_.advance(3);
        process_events();
    }
}

void Emulator::record_fault() noexcept {
    const auto address = cpu_.program_counter();
    const auto set = cpu_.cpsr().instruction_set();
    const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, address};
    const auto opcode = set == InstructionSet::Arm ? bus_.read32(address, access).value
                                                   : bus_.read16(address, access).value;
    fault_ = CpuFault{address, opcode, set};
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

void Emulator::clear_framebuffer() noexcept {
    framebuffer_.fill(Rgba8{0, 0, 0, 255});
}

} // namespace srgba::core
