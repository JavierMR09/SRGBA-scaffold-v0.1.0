#include "test_helpers.hpp"

#include "srgba/core/emulator.hpp"
#include "srgba/core/framebuffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("The emulator starts without a cartridge", "[emulator]") {
    const srgba::core::Emulator emulator;

    REQUIRE_FALSE(emulator.has_rom());
    REQUIRE(emulator.state() == srgba::core::RunState::Empty);
    REQUIRE(emulator.frame_counter() == 0);
    REQUIRE(emulator.framebuffer().size() == srgba::core::kFramebufferPixelCount);
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

    emulator.set_paused(true);
    emulator.run_frame();
    REQUIRE(emulator.is_paused());
    REQUIRE(emulator.frame_counter() == 1);

    emulator.reset();
    REQUIRE_FALSE(emulator.is_paused());
    REQUIRE(emulator.frame_counter() == 0);
}

TEST_CASE("A failed load preserves an empty emulator", "[emulator]") {
    srgba::core::Emulator emulator;
    std::string error;

    REQUIRE_FALSE(emulator.load_rom("this-file-does-not-exist.gba", error));
    REQUIRE_FALSE(error.empty());
    REQUIRE_FALSE(emulator.has_rom());
    REQUIRE(emulator.state() == srgba::core::RunState::Empty);
}
