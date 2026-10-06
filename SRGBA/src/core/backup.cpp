#include "srgba/core/backup.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace srgba::core {
namespace {

// Manufacturer and device IDs reported in flash ID mode. These match chips that shipped in
// cartridges and that games recognize: Panasonic MN63F805MNP (64 KiB) and Sanyo LE26FV10N1TS
// (128 KiB).
constexpr std::array<std::uint8_t, 2> kFlash64Id{0x32U, 0x1BU};
constexpr std::array<std::uint8_t, 2> kFlash128Id{0x62U, 0x13U};
constexpr std::uint32_t kFlashSectorSize = 0x1000U;

struct SaveTag {
    std::string_view text;
    SaveType type;
};

// Longer tags first so "FLASH1M_V" is not mistaken for "FLASH_V".
constexpr std::array<SaveTag, 6> kSaveTags{{
    {"FLASH1M_V", SaveType::Flash128K},
    {"FLASH512_V", SaveType::Flash64K},
    {"FLASH_V", SaveType::Flash64K},
    {"EEPROM_V", SaveType::Eeprom},
    {"SRAM_F_V", SaveType::Sram},
    {"SRAM_V", SaveType::Sram},
}};

} // namespace

std::string_view save_type_name(const SaveType type) noexcept {
    switch (type) {
    case SaveType::None:
        return "None";
    case SaveType::Sram:
        return "SRAM 32 KiB";
    case SaveType::Flash64K:
        return "Flash 64 KiB";
    case SaveType::Flash128K:
        return "Flash 128 KiB";
    case SaveType::Eeprom:
        return "EEPROM";
    }
    return "Unknown";
}

SaveType detect_save_type(const std::span<const std::uint8_t> rom) noexcept {
    // The SDK stores the tag word-aligned.
    for (std::size_t offset = 0; offset + 12U <= rom.size(); offset += 4U) {
        if (rom[offset] != 'S' && rom[offset] != 'F' && rom[offset] != 'E') {
            continue;
        }
        for (const auto& tag : kSaveTags) {
            if (offset + tag.text.size() <= rom.size() &&
                std::memcmp(rom.data() + offset, tag.text.data(), tag.text.size()) == 0) {
                return tag.type;
            }
        }
    }
    return SaveType::Sram;
}

void BackupMemory::configure(const SaveType type) {
    type_ = type;
    std::size_t size = 0;
    switch (type) {
    case SaveType::None:
        break;
    case SaveType::Sram:
        size = kSramSize;
        break;
    case SaveType::Flash64K:
        size = kFlashBankSize;
        break;
    case SaveType::Flash128K:
        size = kFlashBankSize * 2U;
        break;
    case SaveType::Eeprom:
        size = 0; // decided by the first transfer or by the loaded save file
        break;
    }
    data_.assign(size, 0xFFU);
    write_generation_ = 0;
    dirty_ = false;
    flash_state_ = FlashCommandState::Ready;
    flash_id_mode_ = false;
    flash_erase_armed_ = false;
    flash_write_armed_ = false;
    flash_bank_switch_armed_ = false;
    flash_bank_ = 0;
    eeprom_address_bits_ = 0;
    eeprom_bits_.clear();
    eeprom_read_buffer_ = 0;
    eeprom_read_position_ = 68;
}

SaveType BackupMemory::type() const noexcept {
    return type_;
}

std::uint8_t BackupMemory::read8(const std::uint32_t address) const noexcept {
    switch (type_) {
    case SaveType::Sram:
        return data_[address & (kSramSize - 1U)];
    case SaveType::Flash64K:
    case SaveType::Flash128K: {
        const auto offset = address & (kFlashBankSize - 1U);
        if (flash_id_mode_ && offset < 2U) {
            const auto& id = type_ == SaveType::Flash128K ? kFlash128Id : kFlash64Id;
            return id[offset];
        }
        return data_[flash_bank_ * kFlashBankSize + offset];
    }
    case SaveType::None:
    case SaveType::Eeprom:
        return 0xFFU;
    }
    return 0xFFU;
}

void BackupMemory::write8(const std::uint32_t address, const std::uint8_t value) noexcept {
    switch (type_) {
    case SaveType::Sram: {
        auto& cell = data_[address & (kSramSize - 1U)];
        if (cell != value) {
            cell = value;
            mark_written();
        }
        return;
    }
    case SaveType::Flash64K:
    case SaveType::Flash128K:
        flash_write(address & (kFlashBankSize - 1U), value);
        return;
    case SaveType::None:
    case SaveType::Eeprom:
        return;
    }
}

void BackupMemory::flash_write(const std::uint32_t offset, const std::uint8_t value) noexcept {
    if (flash_write_armed_) {
        // Single-byte program command.
        flash_write_armed_ = false;
        data_[flash_bank_ * kFlashBankSize + offset] = value;
        mark_written();
        return;
    }
    if (flash_bank_switch_armed_ && offset == 0U) {
        flash_bank_switch_armed_ = false;
        flash_bank_ = type_ == SaveType::Flash128K ? (value & 1U) : 0U;
        return;
    }

    switch (flash_state_) {
    case FlashCommandState::Ready:
        if (offset == 0x5555U && value == 0xAAU) {
            flash_state_ = FlashCommandState::FirstUnlock;
        } else if (value == 0xF0U) {
            flash_id_mode_ = false; // "terminate" may be written without the unlock sequence
        }
        return;
    case FlashCommandState::FirstUnlock:
        flash_state_ = offset == 0x2AAAU && value == 0x55U ? FlashCommandState::SecondUnlock
                                                           : FlashCommandState::Ready;
        return;
    case FlashCommandState::SecondUnlock:
        flash_state_ = FlashCommandState::Ready;
        if (offset == 0x5555U) {
            switch (value) {
            case 0x90U:
                flash_id_mode_ = true;
                break;
            case 0xF0U:
                flash_id_mode_ = false;
                break;
            case 0x80U:
                flash_erase_armed_ = true;
                break;
            case 0x10U:
                if (flash_erase_armed_) {
                    std::fill(data_.begin(), data_.end(), std::uint8_t{0xFFU});
                    mark_written();
                }
                flash_erase_armed_ = false;
                break;
            case 0xA0U:
                flash_write_armed_ = true;
                break;
            case 0xB0U:
                flash_bank_switch_armed_ = type_ == SaveType::Flash128K;
                break;
            default:
                break;
            }
        } else if (value == 0x30U && flash_erase_armed_) {
            const auto start = flash_bank_ * kFlashBankSize + (offset & ~(kFlashSectorSize - 1U));
            std::fill_n(data_.begin() + static_cast<std::ptrdiff_t>(start), kFlashSectorSize,
                        std::uint8_t{0xFFU});
            flash_erase_armed_ = false;
            mark_written();
        }
        return;
    }
}

bool BackupMemory::is_eeprom() const noexcept {
    return type_ == SaveType::Eeprom;
}

std::size_t BackupMemory::eeprom_address_bits() const noexcept {
    return eeprom_address_bits_;
}

void BackupMemory::set_eeprom_size(const std::size_t address_bits) {
    eeprom_address_bits_ = address_bits;
    const auto size = address_bits == 14U ? kEepromLargeSize : kEepromSmallSize;
    if (data_.size() != size) {
        data_.resize(size, 0xFFU);
    }
}

void BackupMemory::prepare_eeprom_transfer(const std::uint32_t halfwords) noexcept {
    if (!is_eeprom() || eeprom_address_bits_ != 0U) {
        return;
    }
    switch (halfwords) {
    case 9U:  // read request: 2 command bits + 6 address bits + stop bit
    case 73U: // write: 2 + 6 + 64 data bits + stop bit
        set_eeprom_size(6);
        break;
    case 17U: // 2 + 14 + 1
    case 81U: // 2 + 14 + 64 + 1
        set_eeprom_size(14);
        break;
    default:
        break;
    }
}

std::uint16_t BackupMemory::eeprom_read() noexcept {
    if (eeprom_read_position_ >= 68U) {
        return 1; // ready
    }
    const auto position = eeprom_read_position_++;
    if (position < 4U) {
        return 0; // four leading dummy bits
    }
    return static_cast<std::uint16_t>((eeprom_read_buffer_ >> (63U - (position - 4U))) & 1U);
}

void BackupMemory::eeprom_write(const std::uint16_t value) noexcept {
    if (!is_eeprom()) {
        return;
    }
    if (eeprom_address_bits_ == 0U) {
        set_eeprom_size(6); // written without a DMA hint: assume the small chip
    }
    eeprom_bits_.push_back(static_cast<std::uint8_t>(value & 1U));
    if (eeprom_bits_.size() == 2U && eeprom_bits_[0] == 0U) {
        eeprom_bits_.clear(); // not a valid command; resynchronize
        return;
    }
    if (eeprom_bits_.size() < 2U) {
        return;
    }
    const bool read_request = eeprom_bits_[1] == 1U;
    const auto needed = 2U + eeprom_address_bits_ + (read_request ? 0U : 64U) + 1U;
    if (eeprom_bits_.size() >= needed) {
        finish_eeprom_command();
    }
}

void BackupMemory::finish_eeprom_command() noexcept {
    const bool read_request = eeprom_bits_[1] == 1U;
    std::size_t address = 0;
    for (std::size_t bit = 0; bit < eeprom_address_bits_; ++bit) {
        address = (address << 1U) | eeprom_bits_[2U + bit];
    }
    const auto blocks = data_.size() / 8U;
    const auto offset = (address % blocks) * 8U;

    if (read_request) {
        eeprom_read_buffer_ = 0;
        for (std::size_t byte = 0; byte < 8U; ++byte) {
            eeprom_read_buffer_ = (eeprom_read_buffer_ << 8U) | data_[offset + byte];
        }
        eeprom_read_position_ = 0;
    } else {
        const auto data_start = 2U + eeprom_address_bits_;
        for (std::size_t byte = 0; byte < 8U; ++byte) {
            std::uint8_t value = 0;
            for (std::size_t bit = 0; bit < 8U; ++bit) {
                value = static_cast<std::uint8_t>((value << 1U) |
                                                  eeprom_bits_[data_start + byte * 8U + bit]);
            }
            data_[offset + byte] = value;
        }
        eeprom_read_position_ = 68; // write completes immediately: report ready
        mark_written();
    }
    eeprom_bits_.clear();
}

std::span<const std::uint8_t> BackupMemory::data() const noexcept {
    return data_;
}

bool BackupMemory::load(const std::span<const std::uint8_t> bytes) {
    if (type_ == SaveType::None || bytes.empty()) {
        return false;
    }
    if (type_ == SaveType::Eeprom) {
        if (bytes.size() == kEepromSmallSize) {
            set_eeprom_size(6);
        } else if (bytes.size() == kEepromLargeSize) {
            set_eeprom_size(14);
        } else {
            return false;
        }
    }
    const auto count = std::min(bytes.size(), data_.size());
    std::copy_n(bytes.begin(), count, data_.begin());
    dirty_ = false;
    return true;
}

std::uint64_t BackupMemory::write_generation() const noexcept {
    return write_generation_;
}

bool BackupMemory::dirty() const noexcept {
    return dirty_;
}

void BackupMemory::mark_clean() noexcept {
    dirty_ = false;
}

void BackupMemory::mark_written() noexcept {
    ++write_generation_;
    dirty_ = true;
}

} // namespace srgba::core
