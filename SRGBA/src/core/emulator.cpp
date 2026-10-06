#include "srgba/core/emulator.hpp"

#include "srgba/core/checksum.hpp"
#include "srgba/core/hle_bios.hpp"
#include "srgba/core/save_file.hpp"
#include "srgba/core/state_io.hpp"
#include "srgba/core/system_bios.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace srgba::core {
namespace {

constexpr std::uint64_t kMasterCyclesPerFrame = Ppu::kCyclesPerFrame;
// Battery saves are written once the game has stopped writing for half a second.
constexpr std::uint32_t kAutosaveDelayFrames = 30;
constexpr std::size_t kMaximumSaveFileSize = 256U * 1024U;
constexpr std::size_t kMaximumStateFileSize = 4U * 1024U * 1024U;
constexpr std::array<std::uint8_t, 8> kStateMagic{'S', 'R', 'G', 'B', 'A', 'S', 'T', 'A'};
constexpr std::uint64_t kRewindIntervalFrames = 2;
constexpr std::size_t kMaximumCheatFileSize = 1024U * 1024U;
static_assert(kMasterCyclesPerFrame == 280896U);

} // namespace

Emulator::Emulator() {
    bus_.attach_scheduler(scheduler_);
    render_idle_frame();
}

Emulator::~Emulator() {
    static_cast<void>(flush_save());
}

bool Emulator::load_rom(const std::filesystem::path& path, std::string& error_message) {
    try {
        auto cartridge = Cartridge::load(path);
        static_cast<void>(flush_save()); // keep the previous game's progress
        applied_rom_patches_.clear();    // they belonged to the previous cartridge
        cartridge_ = std::move(cartridge);
        rom_crc32_ = crc32(cartridge_->bytes());
        bus_.set_game_pak(cartridge_->bytes());
        bus_.backup().configure(detect_save_type(cartridge_->bytes()));
        load_battery_save();
        load_cheats();
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
    static_cast<void>(flush_save());
    bus_.backup().configure(SaveType::None);
    save_path_.clear();
    rewind_.clear();
    rom_crc32_ = 0;
    cheats_.clear();
    cheat_file_message_.clear();
    applied_rom_patches_.clear();
    rom_patches_stale_ = true;
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

    if (rom_patches_stale_ || cheats_.revision() != synced_cheat_revision_) {
        sync_rom_patches();
    }
    if (cheats_.any_enabled()) {
        cheats_.apply(bus_, pressed_keys_);
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
        update_autosave();
        if (rewind_enabled_ && !suppress_rewind_capture_ &&
            frame_counter_ % kRewindIntervalFrames == 0U) {
            rewind_.push(serialize(false));
        }
    }
}

void Emulator::advance_frame() noexcept {
    if (state_ != RunState::Paused) {
        return;
    }
    state_ = RunState::Running;
    run_frame();
    if (state_ == RunState::Running) {
        state_ = RunState::Paused;
    }
}

std::filesystem::path Emulator::game_file_path(const std::string_view extension) const {
    if (!cartridge_) {
        return {};
    }
    auto file_name = cartridge_->path().filename();
    file_name.replace_extension(std::filesystem::path(std::string(extension)));
    return save_directory_ ? *save_directory_ / file_name
                           : cartridge_->path().parent_path() / file_name;
}

std::vector<std::uint8_t> Emulator::serialize(const bool include_framebuffer) const {
    StateWriter writer(include_framebuffer ? 560U * 1024U : 440U * 1024U);
    writer.bytes(kStateMagic);
    writer.u32(kSaveStateVersion);
    writer.u32(rom_crc32_);
    const auto& code = cartridge_ ? cartridge_->header().game_code : std::string{};
    for (std::size_t index = 0; index < 4U; ++index) {
        writer.u8(index < code.size() ? static_cast<std::uint8_t>(code[index]) : 0U);
    }
    writer.boolean(include_framebuffer);

    cpu_.save_state(writer);
    bus_.save_state(writer);
    ppu_.save_state(writer);
    scheduler_.save_state(writer);
    writer.section("EMU ");
    writer.u64(frame_counter_);
    writer.u64(instruction_counter_);
    writer.boolean(halted_);
    if (include_framebuffer) {
        writer.section("FRAM");
        for (const auto& pixel : framebuffer_) {
            writer.u8(pixel.red);
            writer.u8(pixel.green);
            writer.u8(pixel.blue);
        }
    }
    writer.section("END ");
    return writer.take();
}

bool Emulator::deserialize(const std::span<const std::uint8_t> data, std::string& error_message) {
    StateReader reader(data);
    std::array<std::uint8_t, 8> magic{};
    reader.bytes(magic);
    if (!reader.ok() || magic != kStateMagic) {
        error_message = "This file is not an SRGBA save state.";
        return false;
    }
    if (reader.u32() != kSaveStateVersion) {
        error_message = "This save state was made by an incompatible version of SRGBA.";
        return false;
    }
    if (reader.u32() != rom_crc32_) {
        error_message = "This save state belongs to a different game (or a different ROM dump).";
        return false;
    }
    std::array<std::uint8_t, 4> game_code{};
    reader.bytes(game_code);
    const bool has_framebuffer = reader.boolean();

    cpu_.load_state(reader);
    bus_.load_state(reader);
    ppu_.load_state(reader);
    scheduler_.load_state(reader);
    reader.section("EMU ");
    frame_counter_ = reader.u64();
    instruction_counter_ = reader.u64();
    halted_ = reader.boolean();
    if (has_framebuffer && reader.section("FRAM")) {
        for (auto& pixel : framebuffer_) {
            pixel.red = reader.u8();
            pixel.green = reader.u8();
            pixel.blue = reader.u8();
            pixel.alpha = 255;
        }
    }
    reader.section("END ");
    if (!reader.ok() || !reader.at_end()) {
        error_message = "The save state is damaged or incomplete.";
        return false;
    }
    fault_.reset();
    bus_.set_pressed_keys(pressed_keys_); // input is live; never restored from a state
    error_message.clear();
    return true;
}

std::vector<std::uint8_t> Emulator::save_state() const {
    if (!cartridge_) {
        return {};
    }
    return serialize(true);
}

bool Emulator::load_state(const std::span<const std::uint8_t> data, std::string& error_message) {
    if (!cartridge_) {
        error_message = "Load a game before loading a save state.";
        return false;
    }
    // Keep a copy so a state that fails part-way through cannot leave a half-loaded machine.
    const auto previous = serialize(true);
    if (!deserialize(data, error_message)) {
        std::string ignored;
        static_cast<void>(deserialize(previous, ignored));
        return false;
    }
    if (state_ == RunState::Empty) {
        state_ = RunState::Running;
    }
    return true;
}

std::filesystem::path Emulator::state_slot_path(const int slot) const {
    if (slot < 1 || slot > kStateSlotCount) {
        return {};
    }
    return game_file_path(".ss" + std::to_string(slot));
}

bool Emulator::save_state_slot(const int slot, std::string& error_message) {
    const auto path = state_slot_path(slot);
    if (path.empty()) {
        error_message = "Load a game before saving a state.";
        return false;
    }
    if (save_directory_) {
        std::error_code error;
        std::filesystem::create_directories(*save_directory_, error);
    }
    return write_file_atomically(path, save_state(), error_message);
}

CheatEngine& Emulator::cheats() noexcept {
    return cheats_;
}

const CheatEngine& Emulator::cheats() const noexcept {
    return cheats_;
}

std::filesystem::path Emulator::cheat_file_path() const {
    return game_file_path(".cht");
}

bool Emulator::save_cheats(std::string& error_message) {
    const auto path = cheat_file_path();
    if (path.empty()) {
        error_message = "Load a game before saving cheats.";
        return false;
    }
    if (cheats_.empty()) {
        std::error_code error;
        std::filesystem::remove(path, error);
        error_message.clear();
        return true;
    }
    if (save_directory_) {
        std::error_code error;
        std::filesystem::create_directories(*save_directory_, error);
    }
    const auto text = cheats_.to_text();
    return write_file_atomically(
        path, std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
        error_message);
}

const std::string& Emulator::cheat_file_message() const noexcept {
    return cheat_file_message_;
}

void Emulator::load_cheats() {
    cheats_.clear();
    cheat_file_message_.clear();
    rom_patches_stale_ = true;
    const auto bytes = read_binary_file(cheat_file_path(), kMaximumCheatFileSize);
    if (!bytes) {
        return;
    }
    const std::string_view text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    if (!cheats_.load_text(text, cheat_file_message_)) {
        cheat_file_message_ =
            "Ignored " + cheat_file_path().filename().string() + ": " + cheat_file_message_;
    }
}

void Emulator::sync_rom_patches() {
    rom_patches_stale_ = false;
    synced_cheat_revision_ = cheats_.revision();
    if (!cartridge_) {
        applied_rom_patches_.clear();
        return;
    }
    // Restore in reverse so overlapping patches unwind to the original bytes.
    for (auto patch = applied_rom_patches_.rbegin(); patch != applied_rom_patches_.rend();
         ++patch) {
        static_cast<void>(cartridge_->patch16(patch->offset, patch->original));
    }
    applied_rom_patches_.clear();
    for (const auto& patch : cheats_.rom_patches()) {
        const auto offset = static_cast<std::size_t>(patch.address & 0x01FFFFFEU);
        if (offset + 2U > cartridge_->size()) {
            continue;
        }
        applied_rom_patches_.push_back({offset, cartridge_->patch16(offset, patch.value)});
    }
}

bool Emulator::load_state_slot(const int slot, std::string& error_message) {
    const auto path = state_slot_path(slot);
    if (path.empty()) {
        error_message = "Load a game before loading a state.";
        return false;
    }
    const auto bytes = read_binary_file(path, kMaximumStateFileSize);
    if (!bytes) {
        error_message = "Slot " + std::to_string(slot) + " is empty.";
        return false;
    }
    return load_state(*bytes, error_message);
}

void Emulator::set_rewind_enabled(const bool enabled, const std::size_t memory_limit_bytes) {
    rewind_enabled_ = enabled;
    rewind_.set_memory_limit(memory_limit_bytes);
    if (!enabled) {
        rewind_.clear();
    }
}

bool Emulator::rewind_enabled() const noexcept {
    return rewind_enabled_;
}

bool Emulator::rewind_step() {
    if (!cartridge_ || !rewind_enabled_) {
        return false;
    }
    auto snapshot = rewind_.pop();
    if (!snapshot) {
        return false;
    }
    std::string error;
    if (!deserialize(*snapshot, error)) {
        rewind_.clear();
        return false;
    }
    // Snapshots omit the picture; run the next frame to show the restored moment.
    const auto previous_state = state_;
    state_ = RunState::Running;
    suppress_rewind_capture_ = true;
    run_frame();
    suppress_rewind_capture_ = false;
    if (state_ == RunState::Running) {
        state_ = previous_state;
    }
    return true;
}

std::size_t Emulator::rewind_depth() const noexcept {
    return rewind_.size();
}

std::size_t Emulator::rewind_memory_used() const noexcept {
    return rewind_.memory_used();
}

void Emulator::set_save_directory(std::optional<std::filesystem::path> directory) {
    save_directory_ = std::move(directory);
}

void Emulator::set_battery_saves_enabled(const bool enabled) noexcept {
    battery_saves_enabled_ = enabled;
}

void Emulator::load_battery_save() {
    save_path_.clear();
    save_error_.clear();
    observed_save_generation_ = 0;
    frames_since_save_write_ = 0;
    if (!cartridge_ || bus_.backup().type() == SaveType::None) {
        return;
    }
    save_path_ = game_file_path(".sav");
    if (!battery_saves_enabled_) {
        return;
    }
    if (const auto bytes = read_binary_file(save_path_, kMaximumSaveFileSize)) {
        if (!bus_.backup().load(*bytes)) {
            save_error_ = "The save file does not match this game's save type; it was not loaded.";
        }
    }
}

void Emulator::update_autosave() noexcept {
    auto& backup = bus_.backup();
    if (backup.write_generation() != observed_save_generation_) {
        observed_save_generation_ = backup.write_generation();
        frames_since_save_write_ = 0;
        return;
    }
    if (backup.dirty() && ++frames_since_save_write_ >= kAutosaveDelayFrames) {
        static_cast<void>(flush_save());
    }
}

bool Emulator::flush_save() noexcept {
    auto& backup = bus_.backup();
    if (!battery_saves_enabled_ || !backup.dirty() || save_path_.empty()) {
        return true;
    }
    try {
        if (save_directory_ && !std::filesystem::exists(*save_directory_)) {
            std::filesystem::create_directories(*save_directory_);
        }
    } catch (...) {
        save_error_ = "Could not create the save folder.";
        return false;
    }
    if (!write_file_atomically(save_path_, backup.data(), save_error_)) {
        return false;
    }
    backup.mark_clean();
    ++saves_written_;
    return true;
}

SaveType Emulator::save_type() const noexcept {
    return bus_.backup().type();
}

const std::filesystem::path& Emulator::save_path() const noexcept {
    return save_path_;
}

bool Emulator::save_pending() const noexcept {
    return bus_.backup().dirty();
}

const std::string& Emulator::save_error() const noexcept {
    return save_error_;
}

std::uint64_t Emulator::saves_written() const noexcept {
    return saves_written_;
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

void Emulator::take_audio_samples(std::vector<std::int16_t>& destination) {
    bus_.apu().take_samples(destination);
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
    scheduler_.schedule(EventType::ApuSample, Apu::kCyclesPerSample);
    scheduler_.schedule(EventType::ApuSequencer, Apu::kCyclesPerSequencerStep);
    halted_ = false;
    fault_.reset();
    rewind_.clear();
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
            case EventType::ApuSample:
                bus_.apu().on_sample(event->timestamp);
                scheduler_.schedule_at(EventType::ApuSample,
                                       event->timestamp + Apu::kCyclesPerSample);
                break;
            case EventType::ApuSequencer:
                bus_.apu().on_sequencer_step();
                scheduler_.schedule_at(EventType::ApuSequencer,
                                       event->timestamp + Apu::kCyclesPerSequencerStep);
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
