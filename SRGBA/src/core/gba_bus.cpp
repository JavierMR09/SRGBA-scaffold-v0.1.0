#include "srgba/core/gba_bus.hpp"

#include "srgba/core/system_bios.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>

namespace srgba::core {
namespace {

constexpr std::uint32_t kWaitControlAddress = 0x04000204U;
constexpr std::size_t kDisplayStatusOffset = 0x004U;
constexpr std::size_t kVCountOffset = 0x006U;
constexpr std::size_t kKeyInputOffset = 0x130U;
constexpr std::size_t kKeyControlOffset = 0x132U;
constexpr std::size_t kInterruptEnableOffset = 0x200U;
constexpr std::size_t kInterruptFlagsOffset = 0x202U;
constexpr std::size_t kInterruptMasterOffset = 0x208U;
constexpr std::size_t kHaltControlOffset = 0x301U;
constexpr std::size_t kDmaStart = 0x0B0U;
constexpr std::size_t kDmaEnd = 0x0E0U;
constexpr std::size_t kSoundStart = 0x060U;
constexpr std::size_t kSoundEnd = 0x0B0U;
constexpr std::size_t kTimerStart = 0x100U;
constexpr std::size_t kTimerEnd = 0x110U;
// After the BIOS boot sequence, the last fetched BIOS opcode is "MSR CPSR_fc, r0" (0xE129F000).
constexpr std::uint32_t kPostBootBiosLatch = 0xE129F000U;
constexpr std::uint32_t kPostBootFlagAddress = 0x04000300U;
constexpr std::uint32_t kInternalMemoryControlOffset = 0x00000800U;
constexpr std::uint16_t kWaitControlWritableMask = 0x5FFFU;
constexpr std::uint32_t kGamePakWindowMask = 0x01FFFFFFU;
constexpr std::uint32_t kGamePakBoundaryMask = 0x0001FFFFU;

[[nodiscard]] constexpr std::uint32_t replicate_byte(const std::uint8_t value) noexcept {
    return static_cast<std::uint32_t>(value) * 0x01010101U;
}

[[nodiscard]] constexpr std::uint32_t replicate_halfword(const std::uint16_t value) noexcept {
    return static_cast<std::uint32_t>(value) * 0x00010001U;
}

[[nodiscard]] constexpr std::uint8_t byte_at(const std::uint32_t value,
                                             const std::size_t index) noexcept {
    return static_cast<std::uint8_t>(value >> (index * 8U));
}

} // namespace

GbaBus::GbaBus() noexcept {
    reset();
}

void GbaBus::reset() noexcept {
    ewram_.fill(0);
    iwram_.fill(0);
    io_.fill(0);
    palette_.fill(0);
    vram_.fill(0);
    oam_.fill(0);
    wait_control_ = 0;
    internal_memory_control_ = 0x0D000020U;
    bios_latch_ = 0;
    open_bus_ = 0;
    key_input_ = kKeyMask;
    halt_requested_ = false;
    affine_reload_.fill(true);
    timers_.reset();
    dma_.reset();
    apu_.reset();
}

void GbaBus::initialize_post_bios() noexcept {
    // These values establish the minimum state required by the development boot path. The
    // display remains forced blank until a program configures a video mode.
    io_[0x000] = 0x80U; // DISPCNT forced blank
    io_[0x001] = 0x00U;
    apu_.write(0x088U - kSoundStart, 0x00U); // SOUNDBIAS = 0x0200
    apu_.write(0x089U - kSoundStart, 0x02U);
    io_[0x300] = 0x01U; // POSTFLG
    io_[0x020] = 0x00U; // BG2PA = 0x0100
    io_[0x021] = 0x01U;
    io_[0x026] = 0x00U; // BG2PD = 0x0100
    io_[0x027] = 0x01U;
    io_[0x030] = 0x00U; // BG3PA = 0x0100
    io_[0x031] = 0x01U;
    io_[0x036] = 0x00U; // BG3PD = 0x0100
    io_[0x037] = 0x01U;
    bios_latch_ = kPostBootBiosLatch;
}

bool GbaBus::load_bios(const std::filesystem::path& path, std::string& error_message) noexcept {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            error_message = "SRGBA could not open the selected BIOS file.";
            return false;
        }

        const auto end_position = input.tellg();
        if (end_position < 0 ||
            static_cast<std::uintmax_t>(end_position) != static_cast<std::uintmax_t>(kBiosSize)) {
            error_message = "A Game Boy Advance BIOS image must be exactly 16 KiB.";
            return false;
        }
        if (kBiosSize > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
            error_message = "This build cannot safely read the BIOS image.";
            return false;
        }

        std::vector<std::uint8_t> bytes(kBiosSize);
        input.seekg(0, std::ios::beg);
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            error_message = "SRGBA could not read the complete BIOS file.";
            return false;
        }
        return load_bios(bytes, error_message);
    } catch (...) {
        error_message = "SRGBA could not load the selected BIOS file.";
        return false;
    }
}

bool GbaBus::load_bios(const std::span<const std::uint8_t> bytes,
                       std::string& error_message) noexcept {
    if (bytes.size() != kBiosSize) {
        error_message = "A Game Boy Advance BIOS image must be exactly 16 KiB.";
        return false;
    }

    const bool blank_zero = std::all_of(bytes.begin(), bytes.end(),
                                        [](const std::uint8_t value) { return value == 0x00U; });
    const bool blank_ff = std::all_of(bytes.begin(), bytes.end(),
                                      [](const std::uint8_t value) { return value == 0xFFU; });
    if (blank_zero || blank_ff) {
        error_message = "The selected BIOS image is blank.";
        return false;
    }

    bios_.assign(bytes.begin(), bytes.end());
    bios_crc32_ = crc32(bios_);
    bios_latch_ = 0;
    error_message.clear();
    return true;
}

void GbaBus::unload_bios() noexcept {
    bios_.clear();
    bios_crc32_ = 0;
    bios_latch_ = 0;
}

bool GbaBus::has_bios() const noexcept {
    return bios_.size() == kBiosSize;
}

std::uint32_t GbaBus::bios_crc32() const noexcept {
    return bios_crc32_;
}

void GbaBus::set_game_pak(const std::span<const std::uint8_t> bytes) noexcept {
    game_pak_ = bytes;
}

void GbaBus::clear_game_pak() noexcept {
    game_pak_ = {};
}

bool GbaBus::has_game_pak() const noexcept {
    return !game_pak_.empty();
}

BusReadResult GbaBus::read8(const std::uint32_t address, const BusAccess access) noexcept {
    bool mapped = false;
    const auto byte = read_byte(address, access, mapped);
    const auto value = mapped ? static_cast<std::uint32_t>(byte) : open_bus_value(address, 1U);

    if (mapped) {
        latch_bus_value(value, 1U);
        if (access.kind == AccessKind::Instruction && address < kBiosSize) {
            bios_latch_ = replicate_byte(static_cast<std::uint8_t>(value));
        }
    }
    return {value, access_cycles(address, 1U, access.sequence)};
}

const std::uint8_t* GbaBus::fast_read_pointer(const std::uint32_t aligned_address,
                                              const std::size_t access_width) const noexcept {
    switch (aligned_address >> 24U) {
    case 0x02U:
        return &ewram_[aligned_address & (kEwramSize - 1U)];
    case 0x03U:
        return &iwram_[aligned_address & (kIwramSize - 1U)];
    case 0x05U:
        return &palette_[aligned_address & (kPaletteSize - 1U)];
    case 0x06U:
        return &vram_[vram_offset(aligned_address)];
    case 0x07U:
        return &oam_[aligned_address & (kOamSize - 1U)];
    case 0x08U:
    case 0x09U:
    case 0x0AU:
    case 0x0BU:
    case 0x0CU:
    case 0x0DU: {
        const auto offset = static_cast<std::size_t>(aligned_address & kGamePakWindowMask);
        if (offset + access_width <= game_pak_.size()) {
            return &game_pak_[offset];
        }
        return nullptr;
    }
    default:
        return nullptr;
    }
}

std::uint8_t* GbaBus::fast_write_pointer(const std::uint32_t aligned_address) noexcept {
    switch (aligned_address >> 24U) {
    case 0x02U:
        return &ewram_[aligned_address & (kEwramSize - 1U)];
    case 0x03U:
        return &iwram_[aligned_address & (kIwramSize - 1U)];
    case 0x05U:
        return &palette_[aligned_address & (kPaletteSize - 1U)];
    case 0x06U:
        return &vram_[vram_offset(aligned_address)];
    case 0x07U:
        return &oam_[aligned_address & (kOamSize - 1U)];
    default:
        return nullptr;
    }
}

BusReadResult GbaBus::read16(const std::uint32_t address, const BusAccess access) noexcept {
    const auto aligned_address = address & ~1U;
    if (is_eeprom_address(address)) {
        const auto bit = backup_.eeprom_read();
        latch_bus_value(bit, 2U);
        return {bit, fetch_cycles(address, 2U, access)};
    }
    if (region_for(address) == Region::Sram) {
        // The save chip sits on an 8-bit bus: wider reads repeat the addressed byte.
        const auto value = static_cast<std::uint32_t>(backup_.read8(address)) * 0x0101U;
        latch_bus_value(value, 2U);
        return {value, access_cycles(address, 2U, access.sequence)};
    }
    if (const auto* memory = fast_read_pointer(aligned_address, 2U)) {
        auto halfword = static_cast<std::uint16_t>(memory[0] | (memory[1] << 8U));
        if ((address & 1U) != 0U) {
            halfword = static_cast<std::uint16_t>((halfword >> 8U) | (halfword << 8U));
        }
        latch_bus_value(halfword, 2U);
        return {halfword, fetch_cycles(address, 2U, access)};
    }
    bool low_mapped = false;
    bool high_mapped = false;
    const auto low = read_byte(aligned_address, access, low_mapped);
    const auto high = read_byte(aligned_address + 1U, access, high_mapped);

    std::uint32_t value = open_bus_value(address, 2U);
    if (low_mapped && high_mapped) {
        auto halfword = static_cast<std::uint16_t>(static_cast<std::uint16_t>(low) |
                                                   (static_cast<std::uint16_t>(high) << 8U));
        if ((address & 1U) != 0U) {
            halfword = static_cast<std::uint16_t>((halfword >> 8U) | (halfword << 8U));
        }
        value = halfword;
        latch_bus_value(value, 2U);
        if (access.kind == AccessKind::Instruction && address < kBiosSize) {
            bios_latch_ = replicate_halfword(halfword);
        }
    }
    return {value, fetch_cycles(address, 2U, access)};
}

BusReadResult GbaBus::read32(const std::uint32_t address, const BusAccess access) noexcept {
    const auto aligned_address = address & ~3U;
    if (region_for(address) == Region::Sram) {
        const auto value = replicate_byte(backup_.read8(address));
        latch_bus_value(value, 4U);
        return {value, access_cycles(address, 4U, access.sequence)};
    }
    if (const auto* memory = fast_read_pointer(aligned_address, 4U)) {
        const auto word = static_cast<std::uint32_t>(memory[0]) |
                          (static_cast<std::uint32_t>(memory[1]) << 8U) |
                          (static_cast<std::uint32_t>(memory[2]) << 16U) |
                          (static_cast<std::uint32_t>(memory[3]) << 24U);
        const auto value = std::rotr(word, static_cast<int>((address & 3U) * 8U));
        latch_bus_value(value, 4U);
        return {value, fetch_cycles(address, 4U, access)};
    }
    std::uint32_t value = 0;
    bool all_mapped = true;
    for (std::size_t index = 0; index < 4U; ++index) {
        bool mapped = false;
        const auto byte =
            read_byte(aligned_address + static_cast<std::uint32_t>(index), access, mapped);
        all_mapped = all_mapped && mapped;
        value |= static_cast<std::uint32_t>(byte) << (index * 8U);
    }

    if (all_mapped) {
        const auto rotation = static_cast<int>((address & 3U) * 8U);
        value = std::rotr(value, rotation);
        latch_bus_value(value, 4U);
        if (access.kind == AccessKind::Instruction && address < kBiosSize) {
            bios_latch_ = value;
        }
    } else {
        value = open_bus_value(address, 4U);
    }
    return {value, fetch_cycles(address, 4U, access)};
}

BusWriteResult GbaBus::write8(const std::uint32_t address, const std::uint8_t value,
                              const BusAccess access) noexcept {
    write_byte(address, value, 1U);
    latch_bus_value(value, 1U);
    return {access_cycles(address, 1U, access.sequence)};
}

BusWriteResult GbaBus::write16(const std::uint32_t address, const std::uint16_t value,
                               const BusAccess access) noexcept {
    const auto aligned_address = address & ~1U;
    if (is_eeprom_address(address)) {
        backup_.eeprom_write(value);
        latch_bus_value(value, 2U);
        return {access_cycles(address, 2U, access.sequence)};
    }
    if (region_for(address) == Region::Sram) {
        // Only the byte lane selected by the address reaches the 8-bit save chip.
        backup_.write8(address, static_cast<std::uint8_t>(value >> ((address & 1U) * 8U)));
        latch_bus_value(value, 2U);
        return {access_cycles(address, 2U, access.sequence)};
    }
    if (auto* memory = fast_write_pointer(aligned_address)) {
        memory[0] = static_cast<std::uint8_t>(value);
        memory[1] = static_cast<std::uint8_t>(value >> 8U);
        latch_bus_value(value, 2U);
        return {access_cycles(address, 2U, access.sequence)};
    }
    write_byte(aligned_address, static_cast<std::uint8_t>(value), 2U);
    write_byte(aligned_address + 1U, static_cast<std::uint8_t>(value >> 8U), 2U);
    latch_bus_value(value, 2U);
    return {access_cycles(address, 2U, access.sequence)};
}

BusWriteResult GbaBus::write32(const std::uint32_t address, const std::uint32_t value,
                               const BusAccess access) noexcept {
    const auto aligned_address = address & ~3U;
    if (region_for(address) == Region::Sram) {
        backup_.write8(address, static_cast<std::uint8_t>(value >> ((address & 3U) * 8U)));
        latch_bus_value(value, 4U);
        return {access_cycles(address, 4U, access.sequence)};
    }
    if (auto* memory = fast_write_pointer(aligned_address)) {
        for (std::size_t index = 0; index < 4U; ++index) {
            memory[index] = byte_at(value, index);
        }
        latch_bus_value(value, 4U);
        return {access_cycles(address, 4U, access.sequence)};
    }
    for (std::size_t index = 0; index < 4U; ++index) {
        write_byte(aligned_address + static_cast<std::uint32_t>(index), byte_at(value, index), 4U);
    }
    latch_bus_value(value, 4U);
    return {access_cycles(address, 4U, access.sequence)};
}

std::uint16_t GbaBus::wait_control() const noexcept {
    return wait_control_;
}

bool GbaBus::game_pak_prefetch_enabled() const noexcept {
    return (wait_control_ & (1U << 14U)) != 0U;
}

std::uint8_t GbaBus::post_boot_flag() const noexcept {
    return io_[kPostBootFlagAddress - kIoStart];
}

void GbaBus::attach_scheduler(Scheduler& scheduler) noexcept {
    scheduler_ = &scheduler;
}

Scheduler& GbaBus::scheduler() noexcept {
    return *scheduler_;
}

Timers& GbaBus::timers() noexcept {
    return timers_;
}

const Timers& GbaBus::timers() const noexcept {
    return timers_;
}

DmaController& GbaBus::dma() noexcept {
    return dma_;
}

std::uint8_t GbaBus::on_timer_overflow(const std::size_t index,
                                       const std::uint64_t timestamp) noexcept {
    const auto overflowed = timers_.on_overflow(index, timestamp, *scheduler_, *this);
    // Timers 0 and 1 clock the Direct Sound FIFOs, which request DMA when half empty.
    const auto refill = apu_.on_timer_overflow(overflowed, timestamp);
    if ((refill & Apu::kFifoA) != 0U) {
        dma_.request_sound_fifo(0x040000A0U, *this);
    }
    if ((refill & Apu::kFifoB) != 0U) {
        dma_.request_sound_fifo(0x040000A4U, *this);
    }
    return overflowed;
}

Apu& GbaBus::apu() noexcept {
    return apu_;
}

const Apu& GbaBus::apu() const noexcept {
    return apu_;
}

void GbaBus::trigger_dma(const DmaTiming timing) noexcept {
    dma_.trigger(timing, *this);
}

std::uint32_t GbaBus::take_dma_cycles() noexcept {
    return dma_.take_stall_cycles();
}

bool GbaBus::take_affine_reload(const std::size_t background) noexcept {
    const bool reload = affine_reload_[background];
    affine_reload_[background] = false;
    return reload;
}

BackupMemory& GbaBus::backup() noexcept {
    return backup_;
}

const BackupMemory& GbaBus::backup() const noexcept {
    return backup_;
}

bool GbaBus::is_eeprom_address(const std::uint32_t address) const noexcept {
    if ((address >> 24U) != 0x0DU || !backup_.is_eeprom()) {
        return false;
    }
    // Cartridges up to 16 MiB expose the EEPROM across the whole 0x0D region; 32 MiB carts only
    // in the last 256 bytes.
    return game_pak_.size() <= 16U * 1024U * 1024U || (address & 0x00FFFF00U) == 0x00FFFF00U;
}

bool GbaBus::using_builtin_bios() const noexcept {
    return !has_bios();
}

void GbaBus::set_bios_latch(const std::uint32_t value) noexcept {
    bios_latch_ = value;
}

void GbaBus::request_interrupt(const Interrupt interrupt) noexcept {
    request_interrupts(static_cast<std::uint16_t>(interrupt));
}

void GbaBus::request_interrupts(const std::uint16_t mask) noexcept {
    const auto flags = static_cast<std::uint16_t>(interrupt_flags() | (mask & kInterruptMask));
    io_[kInterruptFlagsOffset] = static_cast<std::uint8_t>(flags);
    io_[kInterruptFlagsOffset + 1U] = static_cast<std::uint8_t>(flags >> 8U);
}

std::uint16_t GbaBus::interrupt_enable() const noexcept {
    return io_register16(kInterruptEnableOffset);
}

std::uint16_t GbaBus::interrupt_flags() const noexcept {
    return io_register16(kInterruptFlagsOffset);
}

bool GbaBus::interrupt_master_enable() const noexcept {
    return (io_[kInterruptMasterOffset] & 1U) != 0U;
}

bool GbaBus::interrupt_pending() const noexcept {
    return (interrupt_enable() & interrupt_flags() & kInterruptMask) != 0U;
}

bool GbaBus::take_halt_request() noexcept {
    const bool requested = halt_requested_;
    halt_requested_ = false;
    return requested;
}

void GbaBus::set_pressed_keys(const std::uint16_t pressed) noexcept {
    const auto updated = static_cast<std::uint16_t>(~pressed & kKeyMask);
    if (updated == key_input_) {
        return;
    }
    key_input_ = updated;
    update_keypad_interrupt();
}

std::uint16_t GbaBus::key_input() const noexcept {
    return key_input_;
}

std::uint16_t GbaBus::io_register16(const std::uint32_t offset) const noexcept {
    const auto index = static_cast<std::size_t>(offset & 0x3FEU);
    if (index == kKeyInputOffset) {
        return key_input_;
    }
    if (index == 0x204U) {
        return wait_control_;
    }
    return static_cast<std::uint16_t>(io_[index] | (io_[index + 1U] << 8U));
}

void GbaBus::set_display_status_flags(const std::uint8_t flags) noexcept {
    io_[kDisplayStatusOffset] =
        static_cast<std::uint8_t>((io_[kDisplayStatusOffset] & ~0x07U) | (flags & 0x07U));
}

void GbaBus::set_vcount(const std::uint8_t line) noexcept {
    io_[kVCountOffset] = line;
    io_[kVCountOffset + 1U] = 0;
}

std::span<const std::uint8_t> GbaBus::palette_ram() const noexcept {
    return palette_;
}

std::span<const std::uint8_t> GbaBus::video_ram() const noexcept {
    return vram_;
}

std::span<const std::uint8_t> GbaBus::object_attribute_memory() const noexcept {
    return oam_;
}

void GbaBus::update_keypad_interrupt() noexcept {
    const auto control = io_register16(kKeyControlOffset);
    if ((control & 0x4000U) == 0U) {
        return;
    }
    const auto selected = static_cast<std::uint16_t>(control & kKeyMask);
    const auto pressed = static_cast<std::uint16_t>(~key_input_ & kKeyMask);
    const bool all_required = (control & 0x8000U) != 0U;
    const bool condition = all_required ? selected != 0U && (pressed & selected) == selected
                                        : (pressed & selected) != 0U;
    if (condition) {
        request_interrupt(Interrupt::Keypad);
    }
}

GbaBus::Region GbaBus::region_for(const std::uint32_t address) noexcept {
    switch (address >> 24U) {
    case 0x00U:
        return address < kBiosSize ? Region::Bios : Region::Unmapped;
    case 0x02U:
        return Region::Ewram;
    case 0x03U:
        return Region::Iwram;
    case 0x04U:
        if (address - kIoStart < kIoSize ||
            (address & 0x0000FFFFU) - kInternalMemoryControlOffset < 4U) {
            return Region::Io;
        }
        return Region::Unmapped;
    case 0x05U:
        return Region::Palette;
    case 0x06U:
        return Region::Vram;
    case 0x07U:
        return Region::Oam;
    case 0x08U:
    case 0x09U:
        return Region::GamePak0;
    case 0x0AU:
    case 0x0BU:
        return Region::GamePak1;
    case 0x0CU:
    case 0x0DU:
        return Region::GamePak2;
    case 0x0EU:
    case 0x0FU:
        return Region::Sram;
    default:
        return Region::Unmapped;
    }
}

std::size_t GbaBus::vram_offset(const std::uint32_t address) noexcept {
    auto offset = static_cast<std::size_t>(address & 0x0001FFFFU);
    if (offset >= kVramSize) {
        offset -= 0x8000U;
    }
    return offset;
}

std::uint32_t GbaBus::crc32(const std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0xFFFFFFFFU;
    for (const auto byte : bytes) {
        value ^= byte;
        for (unsigned bit_index = 0; bit_index < 8U; ++bit_index) {
            const auto mask = 0U - (value & 1U);
            value = (value >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~value;
}

std::uint8_t GbaBus::read_byte(const std::uint32_t address, const BusAccess& access,
                               bool& mapped) noexcept {
    mapped = true;
    switch (region_for(address)) {
    case Region::Bios:
        if (access.kind == AccessKind::Data && access.program_counter >= kBiosSize) {
            return byte_at(bios_latch_, address & 3U);
        }
        return has_bios() ? bios_[address] : builtin_bios_image()[address];

    case Region::Ewram:
        return ewram_[static_cast<std::size_t>(address) & (kEwramSize - 1U)];
    case Region::Iwram:
        return iwram_[static_cast<std::size_t>(address) & (kIwramSize - 1U)];
    case Region::Io: {
        const auto low_address = address & 0x0000FFFFU;
        if (low_address - kInternalMemoryControlOffset < 4U) {
            return byte_at(internal_memory_control_, low_address - kInternalMemoryControlOffset);
        }
        const auto offset = static_cast<std::size_t>(address - kIoStart);
        if (offset == 0x204U) {
            return static_cast<std::uint8_t>(wait_control_);
        }
        if (offset == 0x205U) {
            return static_cast<std::uint8_t>(wait_control_ >> 8U);
        }
        if (offset >= kSoundStart && offset < kSoundEnd) {
            return apu_.read(static_cast<std::uint32_t>(offset - kSoundStart));
        }
        if (offset >= kTimerStart && offset < kTimerEnd) {
            return timers_.read(static_cast<std::uint32_t>(offset - kTimerStart),
                                scheduler_->now());
        }
        if (offset >= kDmaStart && offset < kDmaEnd) {
            return dma_.read(static_cast<std::uint32_t>(offset - kDmaStart));
        }
        if (offset == kKeyInputOffset) {
            return static_cast<std::uint8_t>(key_input_);
        }
        if (offset == kKeyInputOffset + 1U) {
            return static_cast<std::uint8_t>(key_input_ >> 8U);
        }
        if (offset < io_.size()) {
            return io_[offset];
        }
        mapped = false;
        return 0;
    }

    case Region::Palette:
        return palette_[static_cast<std::size_t>(address) & (kPaletteSize - 1U)];
    case Region::Vram:
        return vram_[vram_offset(address)];
    case Region::Oam:
        return oam_[static_cast<std::size_t>(address) & (kOamSize - 1U)];

    case Region::GamePak0:
    case Region::GamePak1:
    case Region::GamePak2: {
        const auto offset = static_cast<std::size_t>(address & kGamePakWindowMask);
        if (offset < game_pak_.size()) {
            return game_pak_[offset];
        }
        // Past the end of the ROM (or with no cartridge) the shared address/data lines read
        // back the lower 16 bits of the halfword address.
        const auto halfword = static_cast<std::uint16_t>((address >> 1U) & 0xFFFFU);
        return (address & 1U) == 0U ? static_cast<std::uint8_t>(halfword)
                                    : static_cast<std::uint8_t>(halfword >> 8U);
    }

    case Region::Sram:
        return backup_.read8(address);
    case Region::Unmapped:
        mapped = false;
        return 0;
    }
    mapped = false;
    return 0;
}

void GbaBus::write_byte(const std::uint32_t address, const std::uint8_t value,
                        const std::size_t access_width) noexcept {
    switch (region_for(address)) {
    case Region::Bios:
        return;
    case Region::Ewram:
        ewram_[static_cast<std::size_t>(address) & (kEwramSize - 1U)] = value;
        return;
    case Region::Iwram:
        iwram_[static_cast<std::size_t>(address) & (kIwramSize - 1U)] = value;
        return;

    case Region::Io: {
        const auto low_address = address & 0x0000FFFFU;
        if (low_address - kInternalMemoryControlOffset < 4U) {
            const auto shift =
                static_cast<unsigned>(low_address - kInternalMemoryControlOffset) * 8U;
            internal_memory_control_ = (internal_memory_control_ & ~(0xFFU << shift)) |
                                       (static_cast<std::uint32_t>(value) << shift);
            return;
        }

        const auto offset = static_cast<std::size_t>(address - kIoStart);
        if (offset == 0x204U) {
            wait_control_ = static_cast<std::uint16_t>((wait_control_ & 0xFF00U) |
                                                       static_cast<std::uint16_t>(value));
            wait_control_ &= kWaitControlWritableMask;
            return;
        }
        if (offset == 0x205U) {
            wait_control_ = static_cast<std::uint16_t>((wait_control_ & 0x00FFU) |
                                                       (static_cast<std::uint16_t>(value) << 8U));
            wait_control_ &= kWaitControlWritableMask;
            return;
        }
        if (offset >= kSoundStart && offset < kSoundEnd) {
            apu_.write(static_cast<std::uint32_t>(offset - kSoundStart), value);
            return;
        }
        if (offset >= kTimerStart && offset < kTimerEnd) {
            timers_.write(static_cast<std::uint32_t>(offset - kTimerStart), value, *scheduler_);
            return;
        }
        if (offset >= kDmaStart && offset < kDmaEnd) {
            dma_.write(static_cast<std::uint32_t>(offset - kDmaStart), value, *this);
            return;
        }
        if (offset >= 0x028U && offset < 0x030U) {
            affine_reload_[0] = true;
        } else if (offset >= 0x038U && offset < 0x040U) {
            affine_reload_[1] = true;
        }

        switch (offset) {
        case kDisplayStatusOffset:
            // VBlank, HBlank, and VCount-match flags are owned by the PPU.
            io_[offset] = static_cast<std::uint8_t>((io_[offset] & 0x07U) | (value & 0x38U));
            return;
        case kVCountOffset:
        case kVCountOffset + 1U:
        case kKeyInputOffset:
        case kKeyInputOffset + 1U:
            return;
        case kKeyControlOffset:
            io_[offset] = value;
            update_keypad_interrupt();
            return;
        case kKeyControlOffset + 1U:
            io_[offset] = static_cast<std::uint8_t>(value & 0xC3U);
            update_keypad_interrupt();
            return;
        case kInterruptEnableOffset + 1U:
            io_[offset] = static_cast<std::uint8_t>(value & 0x3FU);
            return;
        case kInterruptFlagsOffset:
        case kInterruptFlagsOffset + 1U:
            // Writing 1 acknowledges (clears) an interrupt request.
            io_[offset] = static_cast<std::uint8_t>(io_[offset] & ~value);
            return;
        case kInterruptMasterOffset:
            io_[offset] = static_cast<std::uint8_t>(value & 1U);
            return;
        case kInterruptMasterOffset + 1U:
            return;
        case 0x300U:
            io_[offset] = static_cast<std::uint8_t>(value & 1U);
            return;
        case kHaltControlOffset:
            // Bit 7 selects STOP mode; SRGBA treats STOP like HALT until low-power modes matter.
            io_[offset] = value;
            halt_requested_ = true;
            return;
        default:
            break;
        }
        if (offset < io_.size()) {
            io_[offset] = value;
        }
        return;
    }

    case Region::Palette: {
        const auto offset = static_cast<std::size_t>(address) & (kPaletteSize - 1U);
        if (access_width == 1U) {
            const auto aligned = offset & ~std::size_t{1};
            palette_[aligned] = value;
            palette_[aligned + 1U] = value;
        } else {
            palette_[offset] = value;
        }
        return;
    }

    case Region::Vram: {
        const auto offset = vram_offset(address);
        if (access_width == 1U) {
            const auto display_mode = static_cast<std::uint8_t>(io_[0] & 0x7U);
            const std::size_t object_boundary = display_mode >= 3U ? 0x14000U : 0x10000U;
            if (offset >= object_boundary) {
                return;
            }
            const auto aligned = offset & ~std::size_t{1};
            vram_[aligned] = value;
            vram_[aligned + 1U] = value;
        } else {
            vram_[offset] = value;
        }
        return;
    }

    case Region::Oam:
        if (access_width != 1U) {
            oam_[static_cast<std::size_t>(address) & (kOamSize - 1U)] = value;
        }
        return;

    case Region::Sram:
        backup_.write8(address, value);
        return;
    case Region::GamePak0:
    case Region::GamePak1:
    case Region::GamePak2:
    case Region::Unmapped:
        return;
    }
}

std::uint32_t GbaBus::open_bus_value(const std::uint32_t address,
                                     const std::size_t access_width) const noexcept {
    const auto rotation = static_cast<int>((address & 3U) * 8U);
    const auto rotated = std::rotr(open_bus_, rotation);
    if (access_width == 1U) {
        return rotated & 0xFFU;
    }
    if (access_width == 2U) {
        return rotated & 0xFFFFU;
    }
    return rotated;
}

void GbaBus::latch_bus_value(const std::uint32_t value, const std::size_t access_width) noexcept {
    if (access_width == 1U) {
        open_bus_ = replicate_byte(static_cast<std::uint8_t>(value));
    } else if (access_width == 2U) {
        open_bus_ = replicate_halfword(static_cast<std::uint16_t>(value));
    } else {
        open_bus_ = value;
    }
}

std::uint32_t GbaBus::access_cycles(const std::uint32_t address, const std::size_t access_width,
                                    const AccessSequence sequence) const noexcept {
    switch (region_for(address)) {
    case Region::Ewram:
        return access_width == 4U ? 6U : 3U;
    case Region::Palette:
    case Region::Vram:
        return access_width == 4U ? 2U : 1U;
    case Region::GamePak0:
    case Region::GamePak1:
    case Region::GamePak2:
        return game_pak_cycles(address, access_width, sequence);
    case Region::Sram: {
        constexpr std::array<std::uint32_t, 4> waits{4U, 3U, 2U, 8U};
        return 1U + waits[wait_control_ & 0x3U];
    }
    case Region::Bios:
    case Region::Iwram:
    case Region::Io:
    case Region::Oam:
    case Region::Unmapped:
        return 1U;
    }
    return 1U;
}

std::uint32_t GbaBus::fetch_cycles(const std::uint32_t address, const std::size_t access_width,
                                   const BusAccess& access) const noexcept {
    // Approximate Game Pak prefetch: while the CPU executes sequentially from ROM, the buffer
    // runs ahead and serves each opcode halfword in a single cycle. Data accesses and the
    // refill after a branch still pay the full WAITCNT cost.
    const auto region = region_for(address);
    const bool game_pak =
        region == Region::GamePak0 || region == Region::GamePak1 || region == Region::GamePak2;
    if (game_pak && game_pak_prefetch_enabled() && access.kind == AccessKind::Instruction &&
        access.sequence == AccessSequence::Sequential && !access.pipeline_refill) {
        return access_width == 4U ? 2U : 1U;
    }
    return access_cycles(address, access_width, access.sequence);
}

std::uint32_t GbaBus::game_pak_cycles(const std::uint32_t address, const std::size_t access_width,
                                      const AccessSequence sequence) const noexcept {
    constexpr std::array<std::uint32_t, 4> first_waits{4U, 3U, 2U, 8U};
    constexpr std::array<std::array<std::uint32_t, 2>, 3> second_waits{{
        {2U, 1U},
        {4U, 1U},
        {8U, 1U},
    }};

    const auto region = region_for(address);
    const std::size_t window = region == Region::GamePak0   ? 0U
                               : region == Region::GamePak1 ? 1U
                                                            : 2U;
    const std::array<unsigned, 3> first_shifts{2U, 5U, 8U};
    const std::array<unsigned, 3> second_shifts{4U, 7U, 10U};
    const auto first_index =
        static_cast<std::size_t>((wait_control_ >> first_shifts[window]) & 0x3U);
    const auto second_index =
        static_cast<std::size_t>((wait_control_ >> second_shifts[window]) & 0x1U);
    const auto nonsequential_cycles = 1U + first_waits[first_index];
    const auto sequential_cycles = 1U + second_waits[window][second_index];

    const auto aligned_address = access_width == 4U ? address & ~3U : address & ~1U;
    const bool boundary = (aligned_address & kGamePakBoundaryMask) == 0U;
    const auto first_cycles = sequence == AccessSequence::Sequential && !boundary
                                  ? sequential_cycles
                                  : nonsequential_cycles;
    if (access_width != 4U) {
        return first_cycles;
    }

    const bool second_boundary = ((aligned_address + 2U) & kGamePakBoundaryMask) == 0U;
    return first_cycles + (second_boundary ? nonsequential_cycles : sequential_cycles);
}

} // namespace srgba::core
