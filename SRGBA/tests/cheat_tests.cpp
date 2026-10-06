#include "test_helpers.hpp"

#include "srgba/core/cheats.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/interrupts.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace {

using srgba::core::Cheat;
using srgba::core::CheatEngine;
using srgba::core::CheatFormat;
using srgba::core::CheatOperation;
using srgba::core::compile_cheat;
using srgba::core::GbaBus;

struct CodePair {
    std::uint32_t address;
    std::uint32_t value;
};

[[nodiscard]] CodePair decrypted(std::uint32_t address, std::uint32_t value,
                                 const CheatFormat format) {
    srgba::core::decrypt_device_code(address, value, format);
    return {address, value};
}

// Builds the encrypted text of a device code from its decrypted form.
[[nodiscard]] std::string encrypted(std::uint32_t address, std::uint32_t value,
                                    const CheatFormat format) {
    srgba::core::encrypt_device_code(address, value, format);
    std::array<char, 18> text{};
    std::snprintf(text.data(), text.size(), "%08X %08X", address, value);
    return text.data();
}

void run(const std::string& code, GbaBus& bus, const CheatFormat format = CheatFormat::Auto,
         const std::uint16_t keys = 0) {
    std::string error;
    const auto program = compile_cheat(code, format, error);
    INFO(error);
    REQUIRE(program.has_value());
    srgba::core::run_cheat_program(*program, bus, keys);
}

[[nodiscard]] std::filesystem::path unique_directory() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("srgba-cheats-" + std::to_string(stamp));
}

} // namespace

TEST_CASE("Device codes decrypt to published plaintext", "[cheats]") {
    // Pokemon Ruby GameShark v1/v2 master code: the hook, then the "AXVE" game ID.
    auto line = decrypted(0x9E6AC862U, 0x823AB7A8U, CheatFormat::GameShark);
    CHECK(line.address == 0xF8000430U);
    CHECK(line.value == 0x00000101U);
    line = decrypted(0x46B7D9E4U, 0xA709E9E1U, CheatFormat::GameShark);
    CHECK(line.address == 0x45565841U); // "AXVE"
    CHECK(line.value == 0x001DC0DEU);
    // A 32-bit money write.
    line = decrypted(0x1B3E8B0CU, 0xE075B270U, CheatFormat::GameShark);
    CHECK(line.address == 0x22025BC4U);
    CHECK(line.value == 0x18711A00U);

    // Pokemon Emerald Action Replay v3 master code: the hook, then the "BPEE" game ID.
    line = decrypted(0xD8BAE4D9U, 0x4864DCE5U, CheatFormat::ActionReplayV3);
    CHECK(line.address == 0xC40005ECU);
    CHECK(line.value == 0x00008401U);
    line = decrypted(0xA86CDBA5U, 0x19BA49B3U, CheatFormat::ActionReplayV3);
    CHECK(line.address == 0x45455042U); // "BPEE"
    CHECK(line.value == 0x001DC0DEU);

    for (const auto format : {CheatFormat::GameShark, CheatFormat::ActionReplayV3}) {
        std::uint32_t address = 0x12345678U;
        std::uint32_t value = 0x9ABCDEF0U;
        srgba::core::encrypt_device_code(address, value, format);
        srgba::core::decrypt_device_code(address, value, format);
        CHECK(address == 0x12345678U);
        CHECK(value == 0x9ABCDEF0U);
    }
}

TEST_CASE("Automatic detection tells GameShark from Action Replay v3", "[cheats]") {
    std::string error;
    const auto ruby = compile_cheat("9E6AC862 823AB7A8\n46B7D9E4 A709E9E1\n1B3E8B0C-E075B270",
                                    CheatFormat::Auto, error);
    INFO(error);
    REQUIRE(ruby.has_value());
    CHECK(ruby->format == CheatFormat::GameShark);
    CHECK(ruby->game_code == "AXVE");
    REQUIRE(ruby->operations.size() == 3U);
    CHECK(ruby->operations[2].kind == CheatOperation::Kind::Write);
    CHECK(ruby->operations[2].width == 4U);
    CHECK(ruby->operations[2].address == 0x02025BC4U);
    CHECK(ruby->operations[2].value == 0x18711A00U);

    // libretro cheat databases join every part with '+'.
    const auto emerald =
        compile_cheat("D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3", CheatFormat::Auto, error);
    INFO(error);
    REQUIRE(emerald.has_value());
    CHECK(emerald->format == CheatFormat::ActionReplayV3);
    CHECK(emerald->game_code == "BPEE");
}

TEST_CASE("Raw cheats write bytes, halfwords and words", "[cheats]") {
    GbaBus bus;
    run("02000010:7F\n02000020:BEEF\n03000000:12345678", bus);
    CHECK(bus.peek(0x02000010U, 1) == 0x7FU);
    CHECK(bus.peek(0x02000020U, 2) == 0xBEEFU);
    CHECK(bus.peek(0x03000000U, 4) == 0x12345678U);
    // Halfword reads of byte writes see the neighbor untouched.
    CHECK(bus.peek(0x02000010U, 2) == 0x007FU);
}

TEST_CASE("CodeBreaker codes cover writes, conditions, slides and super codes", "[cheats]") {
    GbaBus bus;
    SECTION("writes and read-modify-write") {
        run("82000000 1234\n32000004 0056\n22000000 0001\n62000000 FF0F", bus);
        CHECK(bus.peek(0x02000000U, 2) == 0x1205U);
        CHECK(bus.peek(0x02000004U, 1) == 0x56U);
        run("E2000000 FFFF", bus); // add -1
        CHECK(bus.peek(0x02000000U, 2) == 0x1204U);
    }
    SECTION("conditions guard the next code") {
        bus.poke(0x02000100U, 1, 2);
        run("72000100 0001\n82000102 ABCD", bus);
        CHECK(bus.peek(0x02000102U, 2) == 0xABCDU);
        run("72000100 0002\n82000104 ABCD\n82000106 1111", bus);
        CHECK(bus.peek(0x02000104U, 2) == 0U); // skipped
        CHECK(bus.peek(0x02000106U, 2) == 0x1111U);
        run("A2000100 0001\n82000108 2222", bus); // if not equal
        CHECK(bus.peek(0x02000108U, 2) == 0U);
        run("B2000100 0000\n8200010A 3333", bus); // if greater
        CHECK(bus.peek(0x0200010AU, 2) == 0x3333U);
        run("F2000100 0002\n8200010C 4444", bus); // if any bit of the mask is set
        CHECK(bus.peek(0x0200010CU, 2) == 0U);
    }
    SECTION("button conditions") {
        const std::string code = "D0000020 0001\n82000200 0001";
        run(code, bus, CheatFormat::Auto, 0);
        CHECK(bus.peek(0x02000200U, 2) == 0U);
        run(code, bus, CheatFormat::Auto, static_cast<std::uint16_t>(srgba::core::Key::A));
        CHECK(bus.peek(0x02000200U, 2) == 1U);
    }
    SECTION("slide code") {
        run("42000300 0010\n00010004 0002", bus);
        for (std::uint32_t index = 0; index < 4U; ++index) {
            CHECK(bus.peek(0x02000300U + index * 2U, 2) == 0x10U + index);
        }
        CHECK(bus.peek(0x02000308U, 2) == 0U);
    }
    SECTION("super code") {
        run("52000400 0004\n11223344 5566\n77880000 0000", bus);
        constexpr std::array<std::uint8_t, 8> expected{0x11, 0x22, 0x33, 0x44,
                                                       0x55, 0x66, 0x77, 0x88};
        for (std::size_t index = 0; index < expected.size(); ++index) {
            CHECK(bus.peek(0x02000400U + static_cast<std::uint32_t>(index), 1) == expected[index]);
        }
        CHECK(bus.peek(0x02000408U, 1) == 0U);
    }
}

TEST_CASE("GameShark v1/v2 codes write memory behind conditions", "[cheats]") {
    GbaBus bus;
    constexpr auto format = CheatFormat::GameShark;
    bus.poke(0x02000000U, 0x55U, 2);
    const auto code = encrypted(0x00000000U, 0x001DC0DEU, format) + "\n" + // game ID
                      encrypted(0x02000010U, 0x000000AAU, format) + "\n" + // 8-bit write
                      encrypted(0x12000012U, 0x0000BBBBU, format) + "\n" + // 16-bit write
                      encrypted(0xD2000000U, 0x00000055U, format) + "\n" + // if == 0x55
                      encrypted(0x22000014U, 0xCCCCCCCCU, format) + "\n" +
                      encrypted(0xE0020056U, 0x02000000U, format) + "\n" + // if == 0x56, 2 lines
                      encrypted(0x02000018U, 0x00000011U, format) + "\n" +
                      encrypted(0x02000019U, 0x00000022U, format) + "\n" +
                      encrypted(0x0200001AU, 0x00000033U, format);
    run(code, bus, format);
    CHECK(bus.peek(0x02000010U, 1) == 0xAAU);
    CHECK(bus.peek(0x02000012U, 2) == 0xBBBBU);
    CHECK(bus.peek(0x02000014U, 4) == 0xCCCCCCCCU);
    CHECK(bus.peek(0x02000018U, 1) == 0U);
    CHECK(bus.peek(0x02000019U, 1) == 0U);
    CHECK(bus.peek(0x0200001AU, 1) == 0x33U);
}

TEST_CASE("Action Replay v3 codes fill, compare and follow pointers", "[cheats]") {
    GbaBus bus;
    constexpr auto format = CheatFormat::ActionReplayV3;
    // Addresses use the v3 packing: region nibble in bits 20-23, offset in bits 0-17, and the
    // code type in bits 25-31.
    const auto ar_address = [](const std::uint32_t type, const std::uint32_t target) {
        return (type << 25U) | ((target & 0x0F000000U) >> 4U) | (target & 0x0003FFFFU);
    };
    bus.poke(0x03000100U, 0x02000700U, 4); // a pointer into EWRAM
    bus.poke(0x02000800U, 0x1234U, 2);
    const auto code =
        encrypted(ar_address(0x00, 0x02000600U), 0x00000377U, format) + "\n" + // 8-bit fill x4
        encrypted(ar_address(0x01, 0x02000610U), 0x0002ABCDU, format) + "\n" + // 16-bit fill x3
        encrypted(ar_address(0x02, 0x02000620U), 0xDEADBEEFU, format) + "\n" +
        encrypted(ar_address(0x05, 0x02000800U), 0x00001234U, format) + "\n" + // if == (true)
        encrypted(ar_address(0x02, 0x02000630U), 0x11111111U, format) + "\n" +
        encrypted(ar_address(0x09, 0x02000800U), 0x00001234U, format) + "\n" + // if != (false)
        encrypted(ar_address(0x02, 0x02000634U), 0x22222222U, format) + "\n" +
        encrypted(ar_address(0x21, 0x03000100U), 0x0002BEEFU, format) + "\n" + // *(p + 4)
        encrypted(ar_address(0x41, 0x02000800U), 0x00000001U, format);         // add 1
    run(code, bus, format);
    CHECK(bus.peek(0x02000600U, 4) == 0x77777777U);
    CHECK(bus.peek(0x02000604U, 1) == 0U);
    CHECK(bus.peek(0x02000610U, 2) == 0xABCDU);
    CHECK(bus.peek(0x02000614U, 2) == 0xABCDU);
    CHECK(bus.peek(0x02000616U, 2) == 0U);
    CHECK(bus.peek(0x02000620U, 4) == 0xDEADBEEFU);
    CHECK(bus.peek(0x02000630U, 4) == 0x11111111U);
    CHECK(bus.peek(0x02000634U, 4) == 0U);
    CHECK(bus.peek(0x02000704U, 2) == 0xBEEFU);
    CHECK(bus.peek(0x02000800U, 2) == 0x1235U);
}

TEST_CASE("Malformed and unsupported codes are rejected with the offending line", "[cheats]") {
    std::string error;
    CHECK_FALSE(compile_cheat("", CheatFormat::Auto, error));
    CHECK(error == "Enter at least one code.");
    CHECK_FALSE(compile_cheat("02000000:123", CheatFormat::Auto, error));
    CHECK_FALSE(compile_cheat("hello world", CheatFormat::Auto, error));
    CHECK_FALSE(compile_cheat("82000000", CheatFormat::Auto, error));
    CHECK_FALSE(compile_cheat("82000000 1234\n02000000:12", CheatFormat::Auto, error));
    CHECK(error.find("mix") != std::string::npos);
    CHECK_FALSE(compile_cheat("82000000 1234", CheatFormat::GameShark, error));
    CHECK_FALSE(compile_cheat("9A1B2C3D 1234\n82000000 0001", CheatFormat::Auto, error));
    CHECK(error.rfind("Line 1 (9A1B2C3D 1234): Encrypted CodeBreaker", 0) == 0);
    CHECK_FALSE(compile_cheat("82000000 0001\nD0000010 0001", CheatFormat::Auto, error));
    CHECK(error.rfind("Line 2", 0) == 0);
    CHECK_FALSE(compile_cheat(encrypted(0xDEADFACEU, 0x00001234U, CheatFormat::GameShark),
                              CheatFormat::GameShark, error));
    CHECK(error.find("DEADFACE") != std::string::npos);
    // Comments are ignored.
    CHECK(compile_cheat("# infinite health\n82000000 0063 # max", CheatFormat::Auto, error));
}

TEST_CASE("Cheat files round-trip and keep codes that do not decode", "[cheats]") {
    CheatEngine engine;
    std::string error;
    REQUIRE(engine.add({"Max money", "1B3E8B0C E075B270", CheatFormat::Auto, true}, error));
    REQUIRE(engine.add(
        {"Say \"hi\"", "82000000 0001\n82000002 0002", CheatFormat::CodeBreaker, false}, error));
    CHECK_FALSE(engine.add({"Broken", "zzz", CheatFormat::Auto, true}, error));
    CHECK(engine.size() == 2U);
    CHECK(engine.decoded_format(0) == CheatFormat::GameShark);

    const auto text = engine.to_text();
    CHECK(text.find("cheat1_code = \"82000000 0001+82000002 0002\"") != std::string::npos);
    CheatEngine loaded;
    REQUIRE(loaded.load_text(text, error));
    CHECK(error.empty());
    REQUIRE(loaded.size() == 2U);
    CHECK(loaded.cheat(0).description == "Max money");
    CHECK(loaded.cheat(0).code == "1B3E8B0C E075B270");
    CHECK(loaded.cheat(0).enabled);
    CHECK(loaded.cheat(1).description == "Say 'hi'");
    CHECK(loaded.cheat(1).code == "82000000 0001\n82000002 0002");
    CHECK(loaded.cheat(1).format == CheatFormat::CodeBreaker);
    CHECK_FALSE(loaded.cheat(1).enabled);

    // A libretro database file: no format key, unknown keys, and one unusable code.
    const std::string libretro = "\xEF\xBB\xBF"
                                 "cheats = 2\n\n"
                                 "cheat0_desc = \"Master Code\"\n"
                                 "cheat0_code = \"D8BAE4D9+4864DCE5+A86CDBA5+19BA49B3\"\n"
                                 "cheat0_enable = false\n"
                                 "cheat0_handler = \"1\"\n"
                                 "cheat1_desc = \"Mystery\"\n"
                                 "cheat1_code = \"12345\"\n"
                                 "cheat1_enable = true\n";
    REQUIRE(loaded.load_text(libretro, error));
    CHECK(error == "1 cheat could not be decoded.");
    REQUIRE(loaded.size() == 2U);
    CHECK(loaded.decoded_format(0) == CheatFormat::ActionReplayV3);
    CHECK(loaded.cheat(0).code == "D8BAE4D9 4864DCE5\nA86CDBA5 19BA49B3");
    CHECK(loaded.problem(0).empty());
    CHECK_FALSE(loaded.problem(1).empty());
    CHECK_FALSE(loaded.cheat(1).enabled);
    CHECK(loaded.cheat(1).code == "12345"); // kept as written

    CHECK_FALSE(loaded.load_text("this is not a cheat file", error));
    CHECK(loaded.size() == 2U);
}

TEST_CASE("The emulator runs cheats each frame and keeps them per game", "[cheats][emulator]") {
    const auto directory = unique_directory();
    auto rom_bytes = srgba::tests::make_valid_test_rom();
    rom_bytes.resize(0x400, 0);
    const srgba::tests::TemporaryRom rom(rom_bytes);

    {
        srgba::core::Emulator emulator;
        emulator.set_save_directory(directory);
        emulator.set_battery_saves_enabled(false);
        std::string error;
        REQUIRE(emulator.load_rom(rom.path(), error));
        CHECK(emulator.cheats().empty());

        REQUIRE(emulator.cheats().add({"Lives", "03000010:09", CheatFormat::Auto, true}, error));
        // GameShark ROM patch of the halfword at 0x08000200.
        const auto patch =
            encrypted(0x60000000U | (0x04000000U + 0x100U), 0x0000ABCDU, CheatFormat::GameShark);
        REQUIRE(emulator.cheats().add({"Patch", patch, CheatFormat::Auto, true}, error));
        emulator.run_frame();
        CHECK(emulator.bus().peek(0x03000010U, 1) == 9U);
        CHECK(emulator.bus().peek(0x08000200U, 2) == 0xABCDU);

        // Save states stay compatible while a ROM patch is active.
        const auto state = emulator.save_state();
        CHECK(emulator.load_state(state, error));

        emulator.cheats().set_enabled(1, false);
        emulator.run_frame();
        CHECK(emulator.bus().peek(0x08000200U, 2) == 0U);
        emulator.cheats().set_enabled(1, true);

        REQUIRE(emulator.save_cheats(error));
        CHECK(std::filesystem::exists(emulator.cheat_file_path()));
    }
    {
        srgba::core::Emulator emulator;
        emulator.set_save_directory(directory);
        emulator.set_battery_saves_enabled(false);
        std::string error;
        REQUIRE(emulator.load_rom(rom.path(), error));
        CHECK(emulator.cheat_file_message().empty());
        REQUIRE(emulator.cheats().size() == 2U);
        CHECK(emulator.cheats().cheat(0).description == "Lives");
        emulator.run_frame();
        CHECK(emulator.bus().peek(0x03000010U, 1) == 9U);
        CHECK(emulator.bus().peek(0x08000200U, 2) == 0xABCDU);

        // Clearing the list deletes the file and restores the ROM.
        emulator.cheats().clear();
        REQUIRE(emulator.save_cheats(error));
        CHECK_FALSE(std::filesystem::exists(emulator.cheat_file_path()));
        emulator.run_frame();
        CHECK(emulator.bus().peek(0x08000200U, 2) == 0U);

        emulator.unload_rom();
        CHECK(emulator.cheats().empty());
    }
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}
