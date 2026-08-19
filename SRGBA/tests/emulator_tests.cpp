#include "test_helpers.hpp"

#include "srgba/core/emulator.hpp"
#include "srgba/core/framebuffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("The emulator starts without a cartridge", "[emulator]") {
    const srgba::core::Emulator emulator;

    REQUIRE_FALSE(emulator.has_rom());
    REQUIRE(emulator.state() == srgba::core::RunState::Empty);
    REQUIRE(emulator.frame_counter() == 0);
    REQUIRE(emulator.framebuffer().size() == srgba::core::kFramebufferPixelCount);
    REQUIRE(emulator.cpu().cpsr().mode() == srgba::core::ProcessorMode::Supervisor);
}

TEST_CASE("A loaded cartridge can run, pause, and reset scaffold frames", "[emulator]") {
    const srgba::tests::TemporaryRom rom(srgba::tests::make_valid_test_rom());
    srgba::core::Emulator emulator;
    std::string error;

    REQUIRE(emulator.load_rom(rom.path(), error));
    REQUIRE(error.empty());
    REQUIRE(emulator.has_rom());
    REQUIRE(emulator.state() == srgba::core::RunState::Running);

    emulator.run_frame();
    REQUIRE(emulator.frame_counter() == 1);

    const auto initial_cpu_status = emulator.cpu().cpsr().value();

    emulator.set_paused(true);
    emulator.run_frame();
    REQUIRE(emulator.is_paused());
    REQUIRE(emulator.frame_counter() == 1);

    emulator.reset();
    REQUIRE_FALSE(emulator.is_paused());
    REQUIRE(emulator.frame_counter() == 0);
    REQUIRE(emulator.cpu().cpsr().value() == initial_cpu_status);
}

TEST_CASE("A failed load preserves an empty emulator", "[emulator]") {
    srgba::core::Emulator emulator;
    std::string error;

    REQUIRE_FALSE(emulator.load_rom("this-file-does-not-exist.gba", error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(emulator.has_rom());
    REQUIRE(emulator.state() == srgba::core::RunState::Empty);
}

TEST_CASE("Direct boot initializes post-BIOS state and runs a CPU-focused test ROM",
          "[emulator][boot][integration]") {
    const srgba::tests::TemporaryRom rom(srgba::tests::make_m2_cpu_test_rom());
    srgba::core::Emulator emulator;
    std::string error;

    REQUIRE(emulator.load_rom(rom.path(), error));
    REQUIRE(error.empty());
    REQUIRE_FALSE(emulator.booting_through_bios());
    REQUIRE(emulator.cpu().program_counter() == srgba::core::GbaBus::kGamePakStart);
    REQUIRE(emulator.cpu().cpsr().mode() == srgba::core::ProcessorMode::System);
    REQUIRE(emulator.cpu().register_value(srgba::core::Arm7Tdmi::kStackPointer) == 0x03007F00U);
    REQUIRE(emulator.bus().post_boot_flag() == 1U);

    for (std::size_t instruction = 0; instruction < 6U; ++instruction) {
        const auto result = emulator.step_instruction();
        REQUIRE(result.has_value());
        REQUIRE(result->executed());
    }

    REQUIRE(emulator.cpu().register_value(2) == 43U);
    REQUIRE(emulator.bus().read32(0x02000000U).value == 42U);
    REQUIRE(emulator.bus().read32(0x02000004U).value == 43U);
    REQUIRE(emulator.instruction_counter() == 6U);
    REQUIRE(emulator.cycle_counter() > 0U);
}

TEST_CASE("A user BIOS selects the hardware reset vector and can fall back to direct boot",
          "[emulator][bios][boot]") {
    auto bios_bytes = std::vector<std::uint8_t>(srgba::core::GbaBus::kBiosSize, 0x5AU);
    srgba::tests::write_word(bios_bytes, 0, 0xEAFFFFFEU);
    const srgba::tests::TemporaryRom bios(bios_bytes);
    const srgba::tests::TemporaryRom rom(srgba::tests::make_m2_cpu_test_rom());
    srgba::core::Emulator emulator;
    std::string error;

    emulator.set_boot_mode(srgba::core::BootMode::Bios);
    REQUIRE(emulator.load_bios(bios.path(), error));
    REQUIRE(error.empty());
    REQUIRE(emulator.load_rom(rom.path(), error));
    REQUIRE(emulator.has_bios());
    REQUIRE(emulator.booting_through_bios());
    REQUIRE(emulator.cpu().program_counter() == srgba::core::GbaBus::kBiosStart);
    REQUIRE(emulator.cpu().cpsr().mode() == srgba::core::ProcessorMode::Supervisor);

    emulator.unload_bios();
    REQUIRE_FALSE(emulator.has_bios());
    REQUIRE_FALSE(emulator.booting_through_bios());
    REQUIRE(emulator.cpu().program_counter() == srgba::core::GbaBus::kGamePakStart);
    REQUIRE(emulator.cpu().cpsr().mode() == srgba::core::ProcessorMode::System);
}
