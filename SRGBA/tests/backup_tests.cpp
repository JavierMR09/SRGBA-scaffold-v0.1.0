#include "test_helpers.hpp"

#include "srgba/core/backup.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/gba_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

using srgba::core::BackupMemory;
using srgba::core::GbaBus;
using srgba::core::SaveType;

[[nodiscard]] std::vector<std::uint8_t> rom_with_tag(const std::string_view tag) {
    auto bytes = srgba::tests::make_valid_test_rom();
    bytes.resize(1024, 0);
    for (std::size_t index = 0; index < tag.size(); ++index) {
        bytes[0x200 + index] = static_cast<std::uint8_t>(tag[index]);
    }
    return bytes;
}

void flash_command(GbaBus& bus, const std::uint8_t command) {
    static_cast<void>(bus.write8(0x0E005555U, 0xAAU));
    static_cast<void>(bus.write8(0x0E002AAAU, 0x55U));
    static_cast<void>(bus.write8(0x0E005555U, command));
}

// Sends EEPROM bits through DMA3, as games do: one halfword per bit.
void eeprom_send(GbaBus& bus, const std::vector<std::uint8_t>& bits) {
    for (std::size_t index = 0; index < bits.size(); ++index) {
        static_cast<void>(
            bus.write16(0x02000000U + static_cast<std::uint32_t>(index) * 2U, bits[index]));
    }
    static_cast<void>(bus.write32(0x040000D4U, 0x02000000U));
    static_cast<void>(bus.write32(0x040000D8U, 0x0D000000U));
    static_cast<void>(
        bus.write32(0x040000DCU, 0x80000000U | static_cast<std::uint32_t>(bits.size())));
}

[[nodiscard]] std::vector<std::uint8_t> eeprom_receive(GbaBus& bus) {
    static_cast<void>(bus.write32(0x040000D4U, 0x0D000000U));
    static_cast<void>(bus.write32(0x040000D8U, 0x02001000U));
    static_cast<void>(bus.write32(0x040000DCU, 0x80000000U | 68U));
    std::vector<std::uint8_t> bits;
    for (std::uint32_t index = 0; index < 68U; ++index) {
        bits.push_back(static_cast<std::uint8_t>(bus.read16(0x02001000U + index * 2U).value & 1U));
    }
    return bits;
}

void append_bits(std::vector<std::uint8_t>& bits, const std::uint64_t value,
                 const std::size_t count) {
    for (std::size_t bit = count; bit-- > 0;) {
        bits.push_back(static_cast<std::uint8_t>((value >> bit) & 1U));
    }
}

[[nodiscard]] std::filesystem::path unique_directory() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("srgba-saves-" + std::to_string(stamp));
}

} // namespace

TEST_CASE("Save types are detected from SDK library tags", "[saves]") {
    REQUIRE(srgba::core::detect_save_type(rom_with_tag("FLASH1M_V103")) == SaveType::Flash128K);
    REQUIRE(srgba::core::detect_save_type(rom_with_tag("FLASH512_V131")) == SaveType::Flash64K);
    REQUIRE(srgba::core::detect_save_type(rom_with_tag("FLASH_V126")) == SaveType::Flash64K);
    REQUIRE(srgba::core::detect_save_type(rom_with_tag("EEPROM_V124")) == SaveType::Eeprom);
    REQUIRE(srgba::core::detect_save_type(rom_with_tag("SRAM_V113")) == SaveType::Sram);
    REQUIRE(srgba::core::detect_save_type(srgba::tests::make_valid_test_rom()) == SaveType::Sram);
}

TEST_CASE("SRAM is an 8-bit, mirrored, byte-lane device", "[saves][sram]") {
    GbaBus bus;
    bus.backup().configure(SaveType::Sram);
    REQUIRE(bus.read8(0x0E000010U).value == 0xFFU);

    static_cast<void>(bus.write8(0x0E000010U, 0x5AU));
    REQUIRE(bus.read8(0x0E008010U).value == 0x5AU); // 32 KiB mirror
    REQUIRE(bus.read8(0x0F000010U).value == 0x5AU);
    REQUIRE(bus.read16(0x0E000010U).value == 0x5A5AU);
    REQUIRE(bus.read32(0x0E000010U).value == 0x5A5A5A5AU);

    static_cast<void>(bus.write16(0x0E000021U, 0xAABBU)); // odd address: high byte lane
    REQUIRE(bus.read8(0x0E000021U).value == 0xAAU);
    static_cast<void>(bus.write32(0x0E000032U, 0xAABBCCDDU));
    REQUIRE(bus.read8(0x0E000032U).value == 0xBBU);
    REQUIRE(bus.read8(0x0E000033U).value == 0xFFU);
    REQUIRE(bus.backup().dirty());
}

TEST_CASE("Flash reports its ID and supports program, erase, and banking", "[saves][flash]") {
    GbaBus bus;
    bus.backup().configure(SaveType::Flash128K);

    flash_command(bus, 0x90U);
    REQUIRE(bus.read8(0x0E000000U).value == 0x62U);
    REQUIRE(bus.read8(0x0E000001U).value == 0x13U);
    flash_command(bus, 0xF0U);
    REQUIRE(bus.read8(0x0E000000U).value == 0xFFU);

    // Writes need the A0 program command.
    static_cast<void>(bus.write8(0x0E001234U, 0x11U));
    REQUIRE(bus.read8(0x0E001234U).value == 0xFFU);
    flash_command(bus, 0xA0U);
    static_cast<void>(bus.write8(0x0E001234U, 0x11U));
    REQUIRE(bus.read8(0x0E001234U).value == 0x11U);

    // Bank 1 is independent of bank 0.
    flash_command(bus, 0xB0U);
    static_cast<void>(bus.write8(0x0E000000U, 1U));
    REQUIRE(bus.read8(0x0E001234U).value == 0xFFU);
    flash_command(bus, 0xA0U);
    static_cast<void>(bus.write8(0x0E001234U, 0x22U));
    flash_command(bus, 0xB0U);
    static_cast<void>(bus.write8(0x0E000000U, 0U));
    REQUIRE(bus.read8(0x0E001234U).value == 0x11U);
    REQUIRE(bus.backup().data()[0x11234U] == 0x22U);

    // Sector erase clears one 4 KiB sector of the current bank.
    flash_command(bus, 0xA0U);
    static_cast<void>(bus.write8(0x0E002000U, 0x33U));
    flash_command(bus, 0x80U);
    static_cast<void>(bus.write8(0x0E005555U, 0xAAU));
    static_cast<void>(bus.write8(0x0E002AAAU, 0x55U));
    static_cast<void>(bus.write8(0x0E001000U, 0x30U));
    REQUIRE(bus.read8(0x0E001234U).value == 0xFFU);
    REQUIRE(bus.read8(0x0E002000U).value == 0x33U);

    // Chip erase clears everything.
    flash_command(bus, 0x80U);
    flash_command(bus, 0x10U);
    REQUIRE(bus.read8(0x0E002000U).value == 0xFFU);
    REQUIRE(bus.backup().data()[0x11234U] == 0xFFU);
}

TEST_CASE("EEPROM speaks the serial protocol over DMA and infers its size",
          "[saves][eeprom][dma]") {
    for (const std::size_t address_bits : {std::size_t{6}, std::size_t{14}}) {
        GbaBus bus;
        bus.backup().configure(SaveType::Eeprom);

        // Write 0x0123456789ABCDEF to block 5: "10", address, 64 data bits, stop bit.
        std::vector<std::uint8_t> write{1, 0};
        append_bits(write, 5, address_bits);
        append_bits(write, 0x0123456789ABCDEFULL, 64);
        write.push_back(0);
        eeprom_send(bus, write);
        REQUIRE(bus.backup().eeprom_address_bits() == address_bits);
        REQUIRE(bus.backup().data().size() == (address_bits == 6U ? 512U : 8192U));
        REQUIRE(bus.read16(0x0D000000U).value == 1U); // ready

        // Read it back: "11", address, stop bit, then 4 dummy bits and 64 data bits.
        std::vector<std::uint8_t> request{1, 1};
        append_bits(request, 5, address_bits);
        request.push_back(0);
        eeprom_send(bus, request);
        const auto bits = eeprom_receive(bus);
        std::uint64_t value = 0;
        for (std::size_t index = 4; index < 68U; ++index) {
            value = (value << 1U) | bits[index];
        }
        REQUIRE(value == 0x0123456789ABCDEFULL);
        REQUIRE(bus.backup().data()[5U * 8U] == 0x01U);
        REQUIRE(bus.backup().data()[5U * 8U + 7U] == 0xEFU);
    }
}

TEST_CASE("Battery saves are written atomically and restored on the next load",
          "[saves][persistence][emulator]") {
    const auto directory = unique_directory();
    const srgba::tests::TemporaryRom rom(rom_with_tag("SRAM_V113"));
    std::filesystem::path save_path;
    {
        srgba::core::Emulator emulator;
        emulator.set_save_directory(directory);
        std::string error;
        REQUIRE(emulator.load_rom(rom.path(), error));
        REQUIRE(emulator.save_type() == SaveType::Sram);
        save_path = emulator.save_path();
        REQUIRE(save_path.parent_path() == directory);
        REQUIRE(save_path.extension() == ".sav");

        static_cast<void>(emulator.bus().write8(0x0E000100U, 0x42U));
        REQUIRE(emulator.save_pending());
        emulator.run_frame();
        REQUIRE_FALSE(std::filesystem::exists(save_path)); // waits for writes to settle
        for (int frame = 0; frame < 40; ++frame) {
            emulator.run_frame();
        }
        REQUIRE(std::filesystem::exists(save_path));
        REQUIRE_FALSE(emulator.save_pending());
        REQUIRE(emulator.saves_written() == 1U);
        REQUIRE(std::filesystem::file_size(save_path) == BackupMemory::kSramSize);
        REQUIRE_FALSE(std::filesystem::exists(save_path.string() + ".tmp"));

        // A write right before shutdown is flushed by the destructor.
        static_cast<void>(emulator.bus().write8(0x0E000101U, 0x43U));
    }
    {
        srgba::core::Emulator emulator;
        emulator.set_save_directory(directory);
        std::string error;
        REQUIRE(emulator.load_rom(rom.path(), error));
        REQUIRE(emulator.bus().read8(0x0E000100U).value == 0x42U);
        REQUIRE(emulator.bus().read8(0x0E000101U).value == 0x43U);
        // Resetting the console keeps the battery-backed contents.
        emulator.reset();
        REQUIRE(emulator.bus().read8(0x0E000100U).value == 0x42U);
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
}
