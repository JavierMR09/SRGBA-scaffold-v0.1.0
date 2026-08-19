#pragma once

#include "srgba/core/cartridge.hpp"
#include "srgba/core/framebuffer.hpp"

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

class Emulator {
  public:
    Emulator();

    [[nodiscard]] bool load_rom(const std::filesystem::path& path, std::string& error_message);
    void unload_rom() noexcept;
    void reset() noexcept;
    void run_frame() noexcept;
    void set_paused(bool paused) noexcept;

    [[nodiscard]] bool has_rom() const noexcept;
    [[nodiscard]] bool is_paused() const noexcept;
    [[nodiscard]] RunState state() const noexcept;
    [[nodiscard]] const RomHeader* rom_header() const noexcept;
    [[nodiscard]] const std::filesystem::path* rom_path() const noexcept;
    [[nodiscard]] std::size_t rom_size() const noexcept;
    [[nodiscard]] std::uint64_t frame_counter() const noexcept;
    [[nodiscard]] const Framebuffer& framebuffer() const noexcept;

  private:
    void render_idle_frame() noexcept;
    void render_scaffold_frame() noexcept;

    std::optional<Cartridge> cartridge_;
    Framebuffer framebuffer_{};
    RunState state_{RunState::Empty};
    std::uint64_t frame_counter_{};
};

} // namespace srgba::core
