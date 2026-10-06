#pragma once

#include "srgba/core/arm7tdmi.hpp"
#include "srgba/core/cartridge.hpp"
#include "srgba/core/cheats.hpp"
#include "srgba/core/framebuffer.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/ppu.hpp"
#include "srgba/core/rewind.hpp"
#include "srgba/core/scheduler.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

    // Save states. A state captures the whole machine (including save-chip contents) and is tied
    // to the ROM it was made with. Loading a damaged or foreign state leaves the game untouched.
    static constexpr std::uint32_t kSaveStateVersion = 1;
    static constexpr int kStateSlotCount = 9;
    [[nodiscard]] std::vector<std::uint8_t> save_state() const;
    [[nodiscard]] bool load_state(std::span<const std::uint8_t> data, std::string& error_message);
    // Slot files are "<rom name>.ss1" ... ".ss9" beside the battery save.
    [[nodiscard]] std::filesystem::path state_slot_path(int slot) const;
    [[nodiscard]] bool save_state_slot(int slot, std::string& error_message);
    [[nodiscard]] bool load_state_slot(int slot, std::string& error_message);

    // Rewind keeps a snapshot every other frame (about 20 seconds within the default budget).
    void set_rewind_enabled(bool enabled, std::size_t memory_limit_bytes = 64U * 1024U * 1024U);
    [[nodiscard]] bool rewind_enabled() const noexcept;
    // Steps back two frames and renders the restored moment. Returns false at the start of
    // the recorded history.
    bool rewind_step();
    [[nodiscard]] std::size_t rewind_depth() const noexcept;
    [[nodiscard]] std::size_t rewind_memory_used() const noexcept;
    // Cheats for the loaded game, read from "<rom name>.cht" beside the battery save when the
    // game loads. Enabled cheats run at the start of every frame; ROM patches follow the list
    // automatically. Call save_cheats() after changing the list to keep it.
    [[nodiscard]] CheatEngine& cheats() noexcept;
    [[nodiscard]] const CheatEngine& cheats() const noexcept;
    [[nodiscard]] std::filesystem::path cheat_file_path() const;
    bool save_cheats(std::string& error_message);
    // Problems found while reading the cheat file (empty when it loaded cleanly or was absent).
    [[nodiscard]] const std::string& cheat_file_message() const noexcept;

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
    [[nodiscard]] std::vector<std::uint8_t> serialize(bool include_framebuffer) const;
    [[nodiscard]] bool deserialize(std::span<const std::uint8_t> data, std::string& error_message);
    [[nodiscard]] std::filesystem::path game_file_path(std::string_view extension) const;
    void load_battery_save();
    void load_cheats();
    void sync_rom_patches();
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

    struct AppliedRomPatch {
        std::size_t offset{};
        std::uint16_t original{};
    };
    CheatEngine cheats_{};
    std::string cheat_file_message_;
    std::vector<AppliedRomPatch> applied_rom_patches_;
    std::uint64_t synced_cheat_revision_{};
    bool rom_patches_stale_{true};

    std::uint32_t rom_crc32_{};
    RewindBuffer rewind_{};
    bool rewind_enabled_{};
    bool suppress_rewind_capture_{};
};

} // namespace srgba::core
