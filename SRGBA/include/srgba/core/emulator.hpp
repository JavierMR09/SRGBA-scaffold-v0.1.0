#pragma once

#include "srgba/core/arm7tdmi.hpp"
#include "srgba/core/cartridge.hpp"
#include "srgba/core/framebuffer.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/ppu.hpp"
#include "srgba/core/scheduler.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace srgba::core {

enum class RunState {
    Empty,
    Running,
    Paused,
};

enum class BootMode {
    Direct,
    Bios,
};

// Describes the instruction that stopped emulation when the CPU met an encoding SRGBA does not
// implement yet.
struct CpuFault {
    std::uint32_t address{};
    std::uint32_t opcode{};
    InstructionSet instruction_set{InstructionSet::Arm};
};

class Emulator {
  public:
    Emulator();
    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;
    Emulator(Emulator&&) = delete;
    Emulator& operator=(Emulator&&) = delete;
    // Flushes any unsaved battery save.
    ~Emulator();

    // Battery saves are written as "<rom name>.sav" next to the ROM, or in `directory` when set.
    // Disabling them keeps save memory in RAM only (used by tests and tools).
    void set_save_directory(std::optional<std::filesystem::path> directory);
    void set_battery_saves_enabled(bool enabled) noexcept;
    // Writes pending save data now. Returns false when writing failed (see save_error()).
    bool flush_save() noexcept;
    [[nodiscard]] SaveType save_type() const noexcept;
    [[nodiscard]] const std::filesystem::path& save_path() const noexcept;
    [[nodiscard]] bool save_pending() const noexcept;
    [[nodiscard]] const std::string& save_error() const noexcept;
    // Increments each time save data reaches the disk (lets the frontend show a notice).
    [[nodiscard]] std::uint64_t saves_written() const noexcept;

    [[nodiscard]] bool load_rom(const std::filesystem::path& path, std::string& error_message);
    [[nodiscard]] bool load_bios(const std::filesystem::path& path, std::string& error_message);
    void unload_rom() noexcept;
    void unload_bios() noexcept;
    void reset() noexcept;
    // Runs until the next frame boundary (280,896 master cycles per frame, ~59.73 Hz).
    void run_frame() noexcept;
    // Executes one instruction, or skips one halted span up to the next hardware event.
    [[nodiscard]] std::optional<ExecutionResult> step_instruction() noexcept;
    // Moves the audio generated so far (interleaved stereo int16 at Apu::kSampleRate) into
    // `destination`.
    void take_audio_samples(std::vector<std::int16_t>& destination);
    // Active-high mask of srgba::core::Key bits.
    void set_pressed_keys(std::uint16_t pressed) noexcept;
    void set_paused(bool paused) noexcept;
    void set_boot_mode(BootMode mode) noexcept;

    [[nodiscard]] bool has_rom() const noexcept;
    [[nodiscard]] bool has_bios() const noexcept;
    [[nodiscard]] bool is_paused() const noexcept;
    [[nodiscard]] bool is_halted() const noexcept;
    [[nodiscard]] const std::optional<CpuFault>& fault() const noexcept;
    [[nodiscard]] std::uint16_t pressed_keys() const noexcept;
    [[nodiscard]] bool booting_through_bios() const noexcept;
    [[nodiscard]] RunState state() const noexcept;
    [[nodiscard]] BootMode boot_mode() const noexcept;
    [[nodiscard]] const RomHeader* rom_header() const noexcept;
    [[nodiscard]] const std::filesystem::path* rom_path() const noexcept;
    [[nodiscard]] std::size_t rom_size() const noexcept;
    [[nodiscard]] std::uint64_t frame_counter() const noexcept;
    [[nodiscard]] std::uint64_t instruction_counter() const noexcept;
    [[nodiscard]] std::uint64_t cycle_counter() const noexcept;
    [[nodiscard]] const Framebuffer& framebuffer() const noexcept;
    [[nodiscard]] const Arm7Tdmi& cpu() const noexcept;
    [[nodiscard]] const Ppu& ppu() const noexcept;
    [[nodiscard]] const Scheduler& scheduler() const noexcept;
    [[nodiscard]] GbaBus& bus() noexcept;
    [[nodiscard]] const GbaBus& bus() const noexcept;

  private:
    void reset_machine() noexcept;
    void initialize_direct_boot() noexcept;
    void load_battery_save();
    void update_autosave() noexcept;
    void process_events() noexcept;
    void service_interrupts() noexcept;
    void record_fault() noexcept;
    void render_idle_frame() noexcept;
    void clear_framebuffer() noexcept;

    std::optional<Cartridge> cartridge_;
    Arm7Tdmi cpu_{};
    GbaBus bus_{};
    Framebuffer framebuffer_{};
    Ppu ppu_{};
    Scheduler scheduler_{};
    std::optional<CpuFault> fault_;
    RunState state_{RunState::Empty};
    BootMode boot_mode_{BootMode::Direct};
    std::uint64_t frame_counter_{};
    std::uint64_t instruction_counter_{};
    std::uint16_t pressed_keys_{};
    bool halted_{};

    std::optional<std::filesystem::path> save_directory_;
    std::filesystem::path save_path_;
    bool battery_saves_enabled_{true};
    std::uint64_t observed_save_generation_{};
    std::uint32_t frames_since_save_write_{};
    std::uint64_t saves_written_{};
    std::string save_error_;
};

} // namespace srgba::core
