#include "test_helpers.hpp"

#include "demo_rom.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/interrupts.hpp"
#include "srgba/core/ppu.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

using srgba::core::Emulator;
using srgba::core::Key;
using srgba::core::Ppu;
using srgba::core::Rgba8;
namespace demo = srgba::homebrew;

[[nodiscard]] Rgba8 pixel(const Emulator& emulator, const std::uint32_t x, const std::uint32_t y) {
    return emulator.framebuffer()[y * 240U + x];
}

[[nodiscard]] std::uint32_t ewram(Emulator& emulator, const std::uint32_t address) {
    return emulator.bus().read32(address).value;
}

} // namespace

TEST_CASE("The demo ROM has a valid cartridge header", "[demo]") {
    const auto bytes = demo::build_demo_rom();
    const srgba::tests::TemporaryRom rom(bytes);
    Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));
    REQUIRE(emulator.rom_header()->title == "SRGBA DEMO");
    REQUIRE(emulator.rom_header()->checksum_valid);
}

TEST_CASE("The demo ROM renders, idles on VBlank, and responds to input",
          "[demo][integration][irq][ppu][keypad]") {
    const srgba::tests::TemporaryRom rom(demo::build_demo_rom());
    Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));

    // The demo spends its first frames drawing the background under forced blank.
    for (int frame = 0; frame < 6; ++frame) {
        emulator.run_frame();
    }
    REQUIRE_FALSE(emulator.fault().has_value());
    REQUIRE(emulator.state() == srgba::core::RunState::Running);
    // The VBlank handler counts frames once the main loop is running.
    REQUIRE(ewram(emulator, demo::kDemoFrameCounterAddress) >= 2U);
    // The CPU waits for VBlank in HALT for most of each frame.
    REQUIRE(emulator.is_halted());

    REQUIRE(pixel(emulator, 0, 0) == Ppu::to_rgba(demo::demo_background_color(0, 0)));
    REQUIRE(pixel(emulator, 239, 159) == Ppu::to_rgba(demo::demo_background_color(239, 159)));
    REQUIRE(pixel(emulator, 120, 80) == Ppu::to_rgba(demo::kDemoSquareColor));

    emulator.set_pressed_keys(static_cast<std::uint16_t>(Key::Right));
    for (int frame = 0; frame < 10; ++frame) {
        emulator.run_frame();
    }
    emulator.set_pressed_keys(0);
    emulator.run_frame();
    const auto x = ewram(emulator, demo::kDemoPlayerXAddress);
    REQUIRE(x >= demo::kDemoStartX + 16U);
    REQUIRE(x <= demo::kDemoStartX + 22U);
    REQUIRE(ewram(emulator, demo::kDemoPlayerYAddress) == demo::kDemoStartY);
    // The old position has been restored to the background gradient.
    REQUIRE(pixel(emulator, demo::kDemoStartX, demo::kDemoStartY) ==
            Ppu::to_rgba(demo::demo_background_color(demo::kDemoStartX, demo::kDemoStartY)));
    REQUIRE(pixel(emulator, x + 1U, demo::kDemoStartY + 1U) ==
            Ppu::to_rgba(demo::kDemoSquareColor));

    emulator.set_pressed_keys(Key::A | Key::Up);
    for (int frame = 0; frame < 100; ++frame) {
        emulator.run_frame();
    }
    REQUIRE(ewram(emulator, demo::kDemoPlayerYAddress) == 0U); // clamped at the top edge
    REQUIRE(pixel(emulator, x + 1U, 1U) == Ppu::to_rgba(demo::kDemoSquareAlternateColor));
    REQUIRE(emulator.frame_counter() == 117U);
    // One VBlank interrupt per frame since the handler was installed.
    REQUIRE(ewram(emulator, demo::kDemoFrameCounterAddress) >= 112U);
    REQUIRE_FALSE(emulator.fault().has_value());
}

TEST_CASE("Unsupported instructions pause emulation and report a fault", "[emulator][fault]") {
    auto bytes = srgba::tests::make_valid_test_rom();
    srgba::tests::write_word(bytes, 0, 0xEE000010U); // MCR: no coprocessors on the GBA
    const srgba::tests::TemporaryRom rom(bytes);
    Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));

    emulator.run_frame();
    REQUIRE(emulator.is_paused());
    REQUIRE(emulator.fault().has_value());
    REQUIRE(emulator.fault()->address == 0x08000000U);
    REQUIRE(emulator.fault()->opcode == 0xEE000010U);

    emulator.reset();
    REQUIRE_FALSE(emulator.fault().has_value());
}
