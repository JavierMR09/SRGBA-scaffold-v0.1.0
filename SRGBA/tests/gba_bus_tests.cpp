#include "srgba/core/gba_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using srgba::core::AccessKind;
using srgba::core::AccessSequence;
using srgba::core::BusAccess;
using srgba::core::GbaBus;

} // namespace

TEST_CASE("GBA work RAM regions mirror across their address banks", "[bus][memory]") {
    GbaBus bus;

    static_cast<void>(bus.write32(0x02000010U, 0x12345678U));
    REQUIRE(bus.read32(0x02040010U).value == 0x12345678U);
    REQUIRE(bus.read32(0x02FC0010U).value == 0x12345678U);

    static_cast<void>(bus.write16(0x03000022U, 0xBEEFU));
    REQUIRE(bus.read16(0x03008022U).value == 0xBEEFU);
    REQUIRE(bus.read16(0x03FF8022U).value == 0xBEEFU);
}

TEST_CASE("Palette VRAM and OAM implement GBA mirroring and byte-write rules",
          "[bus][video-memory]") {
    GbaBus bus;

    static_cast<void>(bus.write8(0x05000000U, 0x12U));
    REQUIRE(bus.read16(0x05000000U).value == 0x1212U);
    REQUIRE(bus.read16(0x05000400U).value == 0x1212U);

    static_cast<void>(bus.write8(0x06000020U, 0x34U));
    REQUIRE(bus.read16(0x06000020U).value == 0x3434U);

    static_cast<void>(bus.write16(0x06010000U, 0xCAFEU));
    REQUIRE(bus.read16(0x06018000U).value == 0xCAFEU);
    static_cast<void>(bus.write8(0x06010000U, 0x00U));
    REQUIRE(bus.read16(0x06010000U).value == 0xCAFEU);

    static_cast<void>(bus.write16(0x07000004U, 0xABCDU));
    static_cast<void>(bus.write8(0x07000004U, 0x00U));
    REQUIRE(bus.read16(0x07000404U).value == 0xABCDU);
}

TEST_CASE("Bus accesses apply ARM7TDMI alignment and open-bus rotation", "[bus][alignment]") {
    GbaBus bus;

    static_cast<void>(bus.write32(0x03000000U, 0x44332211U));
    REQUIRE(bus.read32(0x03000001U).value == 0x11443322U);
    REQUIRE(bus.read16(0x03000001U).value == 0x1122U);

    static_cast<void>(bus.write32(0x03000003U, 0xA1B2C3D4U));
    REQUIRE(bus.read32(0x03000000U).value == 0xA1B2C3D4U);
    REQUIRE(bus.read32(0x01000000U).value == 0xA1B2C3D4U);
}

TEST_CASE("All three Game Pak windows expose one ROM with WAITCNT timing", "[bus][gamepak]") {
    GbaBus bus;
    const std::array<std::uint8_t, 8> rom{0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U};
    bus.set_game_pak(rom);

    REQUIRE(bus.read32(0x08000000U).value == 0x44332211U);
    REQUIRE(bus.read32(0x0A000000U).value == 0x44332211U);
    REQUIRE(bus.read32(0x0C000000U).value == 0x44332211U);

    REQUIRE(bus.read16(0x08000000U).cycles == 5U);
    REQUIRE(bus.read16(0x08000002U, {AccessSequence::Sequential}).cycles == 3U);
    REQUIRE(bus.read32(0x08000000U).cycles == 8U);
    REQUIRE(bus.read16(0x08020000U, {AccessSequence::Sequential}).cycles == 5U);

    static_cast<void>(bus.write16(0x04000204U, 0x0018U));
    REQUIRE(bus.wait_control() == 0x0018U);
    REQUIRE(bus.read16(0x08000000U).cycles == 3U);
    REQUIRE(bus.read16(0x08000002U, {AccessSequence::Sequential}).cycles == 2U);
    REQUIRE(bus.read32(0x08000000U).cycles == 5U);
}

TEST_CASE("WAITCNT masks unused bits and reports prefetch configuration", "[bus][io]") {
    GbaBus bus;

    static_cast<void>(bus.write16(0x04000204U, 0xFFFFU));
    REQUIRE(bus.wait_control() == 0x5FFFU);
    REQUIRE(bus.read16(0x04000204U).value == 0x5FFFU);
    REQUIRE(bus.game_pak_prefetch_enabled());

    static_cast<void>(bus.write8(0x04000300U, 0xFFU));
    REQUIRE(bus.post_boot_flag() == 1U);
    bus.reset();
    REQUIRE(bus.post_boot_flag() == 0U);
    bus.initialize_post_bios();
    REQUIRE(bus.post_boot_flag() == 1U);
}

TEST_CASE("BIOS images are validated and protected outside BIOS execution", "[bus][bios]") {
    GbaBus bus;
    std::string error;

    const std::vector<std::uint8_t> too_small(128U, 0x42U);
    REQUIRE_FALSE(bus.load_bios(too_small, error));
    REQUIRE_FALSE(error.empty());

    const std::vector<std::uint8_t> blank(GbaBus::kBiosSize, 0x00U);
    REQUIRE_FALSE(bus.load_bios(blank, error));
    REQUIRE_FALSE(error.empty());

    std::vector<std::uint8_t> bios(GbaBus::kBiosSize, 0x5AU);
    bios[0] = 0x01U;
    bios[1] = 0x00U;
    bios[2] = 0xA0U;
    bios[3] = 0xE3U;
    bios[4] = 0x78U;
    bios[5] = 0x56U;
    bios[6] = 0x34U;
    bios[7] = 0x12U;
    REQUIRE(bus.load_bios(bios, error));
    REQUIRE(error.empty());
    REQUIRE(bus.has_bios());
    REQUIRE(bus.bios_crc32() != 0U);

    const BusAccess opcode_access{AccessSequence::NonSequential, AccessKind::Instruction, 0U};
    REQUIRE(bus.read32(0x00000000U, opcode_access).value == 0xE3A00001U);

    const BusAccess protected_access{AccessSequence::NonSequential, AccessKind::Data, 0x08000000U};
    REQUIRE(bus.read32(0x00000004U, protected_access).value == 0xE3A00001U);

    const BusAccess internal_access{AccessSequence::NonSequential, AccessKind::Data, 4U};
    REQUIRE(bus.read32(0x00000004U, internal_access).value == 0x12345678U);

    bus.unload_bios();
    REQUIRE_FALSE(bus.has_bios());
}

TEST_CASE("An empty Game Pak bus exposes the address pattern used by hardware", "[bus][gamepak]") {
    GbaBus bus;

    REQUIRE(bus.read16(0x08000000U).value == 0x0000U);
    REQUIRE(bus.read16(0x08000002U).value == 0x0001U);
    REQUIRE(bus.read32(0x08000004U).value == 0x00030002U);
}
