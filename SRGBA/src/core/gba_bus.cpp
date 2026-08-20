#include "srgba/core/gba_bus.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>

namespace srgba::core {
namespace {

constexpr std::uint32_t kWaitControlAddress = 0x04000204U;
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
}

void GbaBus::initialize_post_bios() noexcept {
    // These values establish the minimum state required by the development boot path. The
    // display remains forced blank until a program configures a video mode.
    io_[0x000] = 0x80U; // DISPCNT forced blank
    io_[0x001] = 0x00U;
    io_[0x088] = 0x00U; // SOUNDBIAS = 0x0200
    io_[0x089] = 0x02U;
    io_[0x300] = 0x01U; // POSTFLG
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

BusReadResult GbaBus::read16(const std::uint32_t address, const BusAccess access) noexcept {
    const auto aligned_address = address & ~1U;
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
    return {value, access_cycles(address, 2U, access.sequence)};
}

BusReadResult GbaBus::read32(const std::uint32_t address, const BusAccess access) noexcept {
    const auto aligned_address = address & ~3U;
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
    return {value, access_cycles(address, 4U, access.sequence)};
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
    write_byte(aligned_address, static_cast<std::uint8_t>(value), 2U);
    write_byte(aligned_address + 1U, static_cast<std::uint8_t>(value >> 8U), 2U);
    latch_bus_value(value, 2U);
    return {access_cycles(address, 2U, access.sequence)};
}

BusWriteResult GbaBus::write32(const std::uint32_t address, const std::uint32_t value,
                               const BusAccess access) noexcept {
    const auto aligned_address = address & ~3U;
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
        if (!has_bios()) {
            mapped = false;
            return 0;
        }
        if (access.kind == AccessKind::Data && access.program_counter >= kBiosSize) {
            return byte_at(bios_latch_, address & 3U);
        }
        return bios_[address];

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
        if (!game_pak_.empty()) {
            if (offset < game_pak_.size()) {
                return game_pak_[offset];
            }
            mapped = false;
            return 0;
        }
        const auto halfword = static_cast<std::uint16_t>((address >> 1U) & 0xFFFFU);
        return (address & 1U) == 0U ? static_cast<std::uint8_t>(halfword)
                                    : static_cast<std::uint8_t>(halfword >> 8U);
    }

    case Region::Sram:
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
        if (offset == 0x300U) {
            io_[offset] = static_cast<std::uint8_t>(value & 1U);
            return;
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

    case Region::GamePak0:
    case Region::GamePak1:
    case Region::GamePak2:
    case Region::Sram:
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
