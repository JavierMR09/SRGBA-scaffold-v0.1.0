#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace srgba::core {

class StateReader;
class StateWriter;

// Cartridge save memory ("backup media").
enum class SaveType : std::uint8_t {
    None,
    Sram,      // 32 KiB battery-backed SRAM
    Flash64K,  // 64 KiB flash
    Flash128K, // 128 KiB flash, two 64 KiB banks
    Eeprom,    // 512 B or 8 KiB serial EEPROM; the size is inferred from the first DMA transfer
};

[[nodiscard]] std::string_view save_type_name(SaveType type) noexcept;

// Finds the save library tag ("SRAM_V", "FLASH1M_V", "EEPROM_V", ...) that Nintendo's SDK embeds
// in cartridges. ROMs without a tag fall back to SRAM, which homebrew commonly uses.
[[nodiscard]] SaveType detect_save_type(std::span<const std::uint8_t> rom) noexcept;

class BackupMemory {
  public:
    static constexpr std::size_t kSramSize = 32U * 1024U;
    static constexpr std::size_t kFlashBankSize = 64U * 1024U;
    static constexpr std::size_t kEepromSmallSize = 512U;
    static constexpr std::size_t kEepromLargeSize = 8U * 1024U;

    // Erases the contents (0xFF, like fresh hardware) and selects the media type.
    void configure(SaveType type);
    [[nodiscard]] SaveType type() const noexcept;
    // Save-state serialization (see state_io.hpp).
    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

    // SRAM and flash, mapped at 0x0E000000-0x0FFFFFFF on an 8-bit bus.
    [[nodiscard]] std::uint8_t read8(std::uint32_t address) const noexcept;
    void write8(std::uint32_t address, std::uint8_t value) noexcept;

    // EEPROM is accessed one bit per 16-bit transfer, normally through DMA3.
    [[nodiscard]] bool is_eeprom() const noexcept;
    // Called before a DMA transfer to the EEPROM so the address width can be inferred from the
    // request length (9/73 halfwords: 512 B, 17/81 halfwords: 8 KiB).
    void prepare_eeprom_transfer(std::uint32_t halfwords) noexcept;
    [[nodiscard]] std::uint16_t eeprom_read() noexcept;
    void eeprom_write(std::uint16_t value) noexcept;
    [[nodiscard]] std::size_t eeprom_address_bits() const noexcept;

    // Raw contents in the conventional .sav layout shared with other emulators.
    [[nodiscard]] std::span<const std::uint8_t> data() const noexcept;
    // Restores contents from a .sav file. Returns false (and keeps the erased contents) when the
    // data does not fit this media type.
    bool load(std::span<const std::uint8_t> bytes);

    // Incremented on every write; lets the emulator notice when writes have settled.
    [[nodiscard]] std::uint64_t write_generation() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    void mark_clean() noexcept;

  private:
    enum class FlashCommandState : std::uint8_t {
        Ready,
        FirstUnlock,  // received 0xAA at 0x5555
        SecondUnlock, // received 0x55 at 0x2AAA
    };

    void mark_written() noexcept;
    void flash_write(std::uint32_t offset, std::uint8_t value) noexcept;
    void finish_eeprom_command() noexcept;
    void set_eeprom_size(std::size_t address_bits);

    SaveType type_{SaveType::None};
    std::vector<std::uint8_t> data_;
    std::uint64_t write_generation_{};
    bool dirty_{};

    // Flash state.
    FlashCommandState flash_state_{FlashCommandState::Ready};
    bool flash_id_mode_{};
    bool flash_erase_armed_{};
    bool flash_write_armed_{};
    bool flash_bank_switch_armed_{};
    std::size_t flash_bank_{};

    // EEPROM state.
    std::size_t eeprom_address_bits_{};
    std::vector<std::uint8_t> eeprom_bits_;
    std::uint64_t eeprom_read_buffer_{};
    std::size_t eeprom_read_position_{68};
};

} // namespace srgba::core
