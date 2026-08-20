#pragma once

#include "srgba/core/arm7tdmi.hpp"
#include "srgba/core/cartridge.hpp"
#include "srgba/core/framebuffer.hpp"
#include "srgba/core/gba_bus.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

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

class Emulator {
  public:
    Emulator();

    [[nodiscard]] bool load_rom(const std::filesystem::path& path, std::string& error_message);
    [[nodiscard]] bool load_bios(const std::filesystem::path& path, std::string& error_message);
    void unload_rom() noexcept;
    void unload_bios() noexcept;
    void reset() noexcept;
    void run_frame() noexcept;
    [[nodiscard]] std::optional<ExecutionResult> step_instruction() noexcept;
    void set_paused(bool paused) noexcept;
    void set_boot_mode(BootMode mode) noexcept;

    [[nodiscard]] bool has_rom() const noexcept;
    [[nodiscard]] bool has_bios() const noexcept;
    [[nodiscard]] bool is_paused() const noexcept;
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
    [[nodiscard]] GbaBus& bus() noexcept;
    [[nodiscard]] const GbaBus& bus() const noexcept;

  private:
    void reset_machine() noexcept;
    void initialize_direct_boot() noexcept;
    void render_idle_frame() noexcept;
    void render_scaffold_frame() noexcept;

    std::optional<Cartridge> cartridge_;
    Arm7Tdmi cpu_{};
    GbaBus bus_{};
    Framebuffer framebuffer_{};
    RunState state_{RunState::Empty};
    BootMode boot_mode_{BootMode::Direct};
    std::uint64_t frame_counter_{};
    std::uint64_t instruction_counter_{};
    std::uint64_t cycle_counter_{};
};

} // namespace srgba::core
