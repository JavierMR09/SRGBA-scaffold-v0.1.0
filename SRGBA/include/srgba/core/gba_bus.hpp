#pragma once

#include "srgba/core/apu.hpp"
#include "srgba/core/backup.hpp"
#include "srgba/core/dma.hpp"
#include "srgba/core/interrupts.hpp"
#include "srgba/core/scheduler.hpp"
#include "srgba/core/timers.hpp"

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
    // True for the opcode fetches that refill the CPU pipeline after a branch. The Game Pak
    // prefetch buffer is empty at that point, so these always pay full wait states.
    bool pipeline_refill{false};
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

    GbaBus();
    GbaBus(const GbaBus&) = delete;
    GbaBus& operator=(const GbaBus&) = delete;
    GbaBus(GbaBus&&) = delete;
    GbaBus& operator=(GbaBus&&) = delete;
    ~GbaBus() = default;

    // Timers schedule overflow events on the machine's master clock. A bus used on its own (for
    // example in unit tests) runs against a private scheduler instead.
    void attach_scheduler(Scheduler& scheduler) noexcept;
    [[nodiscard]] Scheduler& scheduler() noexcept;

    // Clears volatile machine state while preserving attached BIOS and cartridge images.
    void reset() noexcept;
    void initialize_post_bios() noexcept;

    // Save-state serialization of all memory, IO, and attached hardware (not the BIOS or ROM).
    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

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

    // Side-effect-free access for cheats and tools: no wait states, open-bus latching, or
    // save-chip protocol. `width` is 1, 2 or 4 and the address is aligned down to it. Reads see
    // memory, IO registers (raw), and the ROM; other space reads as zero. Writes reach work RAM,
    // palette, VRAM and OAM directly and IO through the register logic; ROM, BIOS and save
    // memory ignore them.
    [[nodiscard]] std::uint32_t peek(std::uint32_t address, std::size_t width) const noexcept;
    void poke(std::uint32_t address, std::uint32_t value, std::size_t width) noexcept;

    [[nodiscard]] std::uint16_t wait_control() const noexcept;
    [[nodiscard]] bool game_pak_prefetch_enabled() const noexcept;
    [[nodiscard]] std::uint8_t post_boot_flag() const noexcept;

    // Built-in system ROM used when no user BIOS is attached. It provides the hardware exception
    // vectors and the interrupt dispatcher; SWI calls are completed by the HLE BIOS.
    [[nodiscard]] bool using_builtin_bios() const noexcept;
    void set_bios_latch(std::uint32_t value) noexcept;

    // Interrupt controller (IE, IF, IME).
    void request_interrupt(Interrupt interrupt) noexcept;
    void request_interrupts(std::uint16_t mask) noexcept;
    [[nodiscard]] std::uint16_t interrupt_enable() const noexcept;
    [[nodiscard]] std::uint16_t interrupt_flags() const noexcept;
    [[nodiscard]] bool interrupt_master_enable() const noexcept;
    [[nodiscard]] bool interrupt_pending() const noexcept;

    // Returns true once after the CPU writes HALTCNT.
    [[nodiscard]] bool take_halt_request() noexcept;

    // Keypad. `pressed` is an active-high mask of srgba::core::Key bits.
    void set_pressed_keys(std::uint16_t pressed) noexcept;
    [[nodiscard]] std::uint16_t key_input() const noexcept;

    // Cartridge save memory. Its contents survive bus resets, like a battery-backed chip.
    [[nodiscard]] BackupMemory& backup() noexcept;
    [[nodiscard]] const BackupMemory& backup() const noexcept;
    [[nodiscard]] bool is_eeprom_address(std::uint32_t address) const noexcept;

    // Sound.
    [[nodiscard]] Apu& apu() noexcept;
    [[nodiscard]] const Apu& apu() const noexcept;

    // Timers and DMA.
    [[nodiscard]] Timers& timers() noexcept;
    [[nodiscard]] const Timers& timers() const noexcept;
    [[nodiscard]] DmaController& dma() noexcept;
    std::uint8_t on_timer_overflow(std::size_t index, std::uint64_t timestamp) noexcept;
    void trigger_dma(DmaTiming timing) noexcept;
    [[nodiscard]] std::uint32_t take_dma_cycles() noexcept;

    // Returns true once after the CPU writes BG2X/BG2Y (background 0) or BG3X/BG3Y (1); the PPU
    // then reloads its internal affine reference point.
    [[nodiscard]] bool take_affine_reload(std::size_t background) noexcept;

    // Video-facing views used by the PPU.
    [[nodiscard]] std::uint16_t io_register16(std::uint32_t offset) const noexcept;
    void set_display_status_flags(std::uint8_t flags) noexcept;
    void set_vcount(std::uint8_t line) noexcept;
    [[nodiscard]] std::span<const std::uint8_t> palette_ram() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> video_ram() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> object_attribute_memory() const noexcept;

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

    // Direct pointers into plain memory for aligned 16/32-bit accesses (nullptr when the access
    // needs the general path: BIOS, IO, SRAM, unmapped space, or past the end of the ROM).
    [[nodiscard]] const std::uint8_t* fast_read_pointer(std::uint32_t aligned_address,
                                                        std::size_t access_width) const noexcept;
    [[nodiscard]] std::uint8_t* fast_write_pointer(std::uint32_t aligned_address) noexcept;

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
    [[nodiscard]] std::uint32_t fetch_cycles(std::uint32_t address, std::size_t access_width,
                                             const BusAccess& access) const noexcept;
    void update_keypad_interrupt() noexcept;

    std::vector<std::uint8_t> bios_;
    std::span<const std::uint8_t> game_pak_{};
    // The large memories live on the heap so an Emulator stays small enough for a thread stack
    // (Windows gives the main thread 1 MiB).
    std::vector<std::uint8_t> ewram_ = std::vector<std::uint8_t>(kEwramSize);
    std::vector<std::uint8_t> iwram_ = std::vector<std::uint8_t>(kIwramSize);
    std::array<std::uint8_t, kIoSize> io_{};
    std::array<std::uint8_t, kPaletteSize> palette_{};
    std::vector<std::uint8_t> vram_ = std::vector<std::uint8_t>(kVramSize);
    std::array<std::uint8_t, kOamSize> oam_{};

    std::uint16_t wait_control_{};
    std::uint32_t internal_memory_control_{0x0D000020U};
    std::uint32_t bios_crc32_{};
    std::uint32_t bios_latch_{};
    std::uint32_t open_bus_{};
    std::uint16_t key_input_{kKeyMask};
    bool halt_requested_{};
    std::array<bool, 2> affine_reload_{};
    Scheduler own_scheduler_{};
    Scheduler* scheduler_{&own_scheduler_};
    Timers timers_{};
    DmaController dma_{};
    BackupMemory backup_{};
    Apu apu_{};
};

} // namespace srgba::core
