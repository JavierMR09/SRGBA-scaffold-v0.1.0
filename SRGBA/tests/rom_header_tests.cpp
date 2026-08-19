#include "test_helpers.hpp"

#include "srgba/core/cartridge.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

TEST_CASE("A valid GBA header is parsed", "[cartridge][header]") {
    const auto bytes = srgba::tests::make_valid_test_rom();
    const auto header = srgba::core::parse_rom_header(bytes);

    REQUIRE(header.title == "SRGBA TEST");
    REQUIRE(header.game_code == "SRGE");
    REQUIRE(header.maker_code == "01");
    REQUIRE(header.unit_code == 0);
    REQUIRE(header.software_version == 1);
    REQUIRE(header.fixed_value_valid);
    REQUIRE(header.checksum_valid);
    REQUIRE(header.is_valid());
}

TEST_CASE("Header validation reports a checksum mismatch", "[cartridge][header]") {
    auto bytes = srgba::tests::make_valid_test_rom();
    bytes[0xA0] ^= 0x01U;

    const auto header = srgba::core::parse_rom_header(bytes);
    REQUIRE(header.fixed_value_valid);
    REQUIRE_FALSE(header.checksum_valid);
    REQUIRE_FALSE(header.is_valid());
}

TEST_CASE("Files smaller than the GBA header are rejected", "[cartridge][header]") {
    const std::vector<std::uint8_t> bytes(srgba::core::kGbaHeaderSize - 1U, 0);
    REQUIRE_THROWS_AS(srgba::core::parse_rom_header(bytes), std::invalid_argument);
}

TEST_CASE("A cartridge loads bytes and metadata from disk", "[cartridge]") {
    const auto bytes = srgba::tests::make_valid_test_rom();
    const srgba::tests::TemporaryRom rom(bytes);

    const auto cartridge = srgba::core::Cartridge::load(rom.path());
    REQUIRE(cartridge.size() == bytes.size());
    REQUIRE(cartridge.header().title == "SRGBA TEST");
    REQUIRE(cartridge.header().is_valid());
}
