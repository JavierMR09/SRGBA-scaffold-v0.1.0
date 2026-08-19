#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace srgba::core {

enum class AccessSequence : std::uint8_t {
    NonSequential,
    Sequential,
};

enum class AccessKind : std::uint8_t {
    Data,
    Instruction,
};

struct BusAccess {
    AccessSequence sequence{AccessSequence::NonSequential};
    AccessKind kind{AccessKind::Data};
    std::uint32_t program_counter{0xFFFFFFFFU};
};

struct BusReadResult {
    std::uint32_t value{};
    std::uint32_t cycles{1};
};

struct BusWriteResult {
    std::uint32_t cycles{1};
};

class GbaBus {
  public:
    static constexpr std::size_t kBiosSize = 16U * 1024U;
    static constexpr std::size_t kEwramSize = 256U * 1024U;
    static constexpr std::size_t kIwramSize = 32U * 1024U;
    static constexpr std::size_t kIoSize = 1024U;
    static constexpr std::size_t kPaletteSize = 1024U;
    static constexpr std::size_t kVramSize = 96U * 1024U;
    static constexpr std::size_t kOamSize = 1024U;

    static constexpr std::uint32_t kBiosStart = 0x00000000U;
    static constexpr std::uint32_t kEwramStart = 0x02000000U;
    static constexpr std::uint32_t kIwramStart = 0x03000000U;
    static constexpr std::uint32_t kIoStart = 0x04000000U;
    static constexpr std::uint32_t kPaletteStart = 0x05000000U;
    static constexpr std::uint32_t kVramStart = 0x06000000U;
    static constexpr std::uint32_t kOamStart = 0x07000000U;
    static constexpr std::uint32_t kGamePakStart = 0x08000000U;

    GbaBus() noexcept;

    // Clears volatile machine state while preserving attached BIOS and cartridge images.
    void reset() noexcept;
    void initialize_post_bios() noexcept;

    [[nodiscard]] bool load_bios(const std::filesystem::path& path,
                                 std::string& error_message) noexcept;
    [[nodiscard]] bool load_bios(std::span<const std::uint8_t> bytes,
                                 std::string& error_message) noexcept;
    void unload_bios() noexcept;
    [[nodiscard]] bool has_bios() const noexcept;
    [[nodiscard]] std::uint32_t bios_crc32() const noexcept;

    void set_game_pak(std::span<const std::uint8_t> bytes) noexcept;
    void clear_game_pak() noexcept;
    [[nodiscard]] bool has_game_pak() const noexcept;

    [[nodiscard]] BusReadResult read8(std::uint32_t address, BusAccess access = {}) noexcept;
    [[nodiscard]] BusReadResult read16(std::uint32_t address, BusAccess access = {}) noexcept;
    [[nodiscard]] BusReadResult read32(std::uint32_t address, BusAccess access = {}) noexcept;

    [[nodiscard]] BusWriteResult write8(std::uint32_t address, std::uint8_t value,
                                        BusAccess access = {}) noexcept;
    [[nodiscard]] BusWriteResult write16(std::uint32_t address, std::uint16_t value,
                                         BusAccess access = {}) noexcept;
    [[nodiscard]] BusWriteResult write32(std::uint32_t address, std::uint32_t value,
                                         BusAccess access = {}) noexcept;

    [[nodiscard]] std::uint16_t wait_control() const noexcept;
    [[nodiscard]] bool game_pak_prefetch_enabled() const noexcept;
    [[nodiscard]] std::uint8_t post_boot_flag() const noexcept;

  private:
    enum class Region : std::uint8_t {
        Bios,
        Ewram,
        Iwram,
        Io,
        Palette,
        Vram,
        Oam,
        GamePak0,
        GamePak1,
        GamePak2,
        Sram,
        Unmapped,
    };

    [[nodiscard]] static Region region_for(std::uint32_t address) noexcept;
    [[nodiscard]] static std::size_t vram_offset(std::uint32_t address) noexcept;
    [[nodiscard]] static std::uint32_t crc32(std::span<const std::uint8_t> bytes) noexcept;

    [[nodiscard]] std::uint8_t read_byte(std::uint32_t address, const BusAccess& access,
                                         bool& mapped) noexcept;
    void write_byte(std::uint32_t address, std::uint8_t value, std::size_t access_width) noexcept;
    [[nodiscard]] std::uint32_t open_bus_value(std::uint32_t address,
                                               std::size_t access_width) const noexcept;
    void latch_bus_value(std::uint32_t value, std::size_t access_width) noexcept;
    [[nodiscard]] std::uint32_t access_cycles(std::uint32_t address, std::size_t access_width,
                                              AccessSequence sequence) const noexcept;
    [[nodiscard]] std::uint32_t game_pak_cycles(std::uint32_t address, std::size_t access_width,
                                                AccessSequence sequence) const noexcept;

    std::vector<std::uint8_t> bios_;
    std::span<const std::uint8_t> game_pak_{};
    std::array<std::uint8_t, kEwramSize> ewram_{};
    std::array<std::uint8_t, kIwramSize> iwram_{};
    std::array<std::uint8_t, kIoSize> io_{};
    std::array<std::uint8_t, kPaletteSize> palette_{};
    std::array<std::uint8_t, kVramSize> vram_{};
    std::array<std::uint8_t, kOamSize> oam_{};

    std::uint16_t wait_control_{};
    std::uint32_t internal_memory_control_{0x0D000020U};
    std::uint32_t bios_crc32_{};
    std::uint32_t bios_latch_{};
    std::uint32_t open_bus_{};
};

} // namespace srgba::core
