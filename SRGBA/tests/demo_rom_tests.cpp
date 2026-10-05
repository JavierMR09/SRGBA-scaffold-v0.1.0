#include "test_helpers.hpp"

#include "demo_rom.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/interrupts.hpp"
#include "srgba/core/ppu.hpp"
#include "tiles_demo_rom.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

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

namespace {

[[nodiscard]] std::uint16_t half(const std::uint16_t color) {
    std::uint16_t result = 0;
    for (unsigned shift = 0; shift < 15U; shift += 5U) {
        result =
            static_cast<std::uint16_t>(result | ((((color >> shift) & 0x1FU) * 8U >> 4U) << shift));
    }
    return result;
}

[[nodiscard]] std::uint16_t brightened(const std::uint16_t color, const unsigned evy) {
    std::uint16_t result = 0;
    for (unsigned shift = 0; shift < 15U; shift += 5U) {
        const auto channel = (color >> shift) & 0x1FU;
        result = static_cast<std::uint16_t>(result |
                                            ((channel + (((31U - channel) * evy) >> 4U)) << shift));
    }
    return result;
}

} // namespace

TEST_CASE("The tiles demo exercises DMA, timers, tiles, sprites, windows, and blending",
          "[demo][integration][ppu][dma][timers]") {
    const srgba::tests::TemporaryRom rom(srgba::homebrew::build_tiles_demo_rom());
    Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));
    REQUIRE(emulator.rom_header()->checksum_valid);
    auto& bus = emulator.bus();

    for (int frame = 0; frame < 60; ++frame) {
        emulator.run_frame();
    }
    REQUIRE_FALSE(emulator.fault().has_value());
    REQUIRE(ewram(emulator, srgba::homebrew::kTilesDemoFrameCounterAddress) >= 55U);

    // DMA loaded the sprite graphics and palettes.
    REQUIRE(bus.read16(0x05000202U).value == srgba::homebrew::kTilesDemoBallOutline);
    REQUIRE(bus.read16(0x06000020U).value == 0x1111U);
    REQUIRE(bus.read16(0x06000040U).value == 0x2222U);

    const auto x = ewram(emulator, srgba::homebrew::kTilesDemoBallXAddress);
    const auto y = ewram(emulator, srgba::homebrew::kTilesDemoBallYAddress);
    REQUIRE(x == srgba::homebrew::kTilesDemoStartX);
    REQUIRE(y == srgba::homebrew::kTilesDemoStartY);
    REQUIRE(srgba::homebrew::tiles_demo_ball_pixel(8, 12) == 2U);
    REQUIRE(pixel(emulator, x + 8U, y + 12U) == Ppu::to_rgba(srgba::homebrew::kTilesDemoBallFill));

    // The shadow (sprite 1) is black at 50% over the checkerboard.
    constexpr auto offset = srgba::homebrew::kTilesDemoShadowOffset;
    const auto shadow = pixel(emulator, x + offset + 14U, y + offset + 8U);
    REQUIRE((shadow == Ppu::to_rgba(half(srgba::homebrew::kTilesDemoLightTile)) ||
             shadow == Ppu::to_rgba(half(srgba::homebrew::kTilesDemoDarkTile))));

    // Window 0 brightens the checkerboard inside a band positioned from timer 2. The register
    // already holds next frame's position (written during VBlank); the band moves about one
    // pixel per frame, so sample a few pixels away from its edges.
    const auto band = bus.io_register16(0x040U);
    const auto left = static_cast<std::uint32_t>(band >> 8U);
    const auto right = static_cast<std::uint32_t>(band & 0xFFU);
    REQUIRE(right > left + 8U);
    const auto band_pixel = pixel(emulator, left + 4U, 150);
    REQUIRE((band_pixel == Ppu::to_rgba(brightened(srgba::homebrew::kTilesDemoLightTile, 6)) ||
             band_pixel == Ppu::to_rgba(brightened(srgba::homebrew::kTilesDemoDarkTile, 6))));
    if (left >= 4U) {
        const auto outside = pixel(emulator, left - 4U, 150);
        REQUIRE((outside == Ppu::to_rgba(srgba::homebrew::kTilesDemoLightTile) ||
                 outside == Ppu::to_rgba(srgba::homebrew::kTilesDemoDarkTile)));
    }

    // The background scrolls every frame and the band follows the timer.
    const auto scroll = bus.io_register16(0x010U);
    for (int frame = 0; frame < 30; ++frame) {
        emulator.run_frame();
    }
    REQUIRE(bus.io_register16(0x010U) == scroll + 30U);
    REQUIRE(bus.io_register16(0x040U) != band);

    // Pressing A plays a chirp on square channel 1; B plays noise on channel 4.
    std::vector<std::int16_t> silence;
    emulator.take_audio_samples(silence);
    emulator.set_pressed_keys(static_cast<std::uint16_t>(Key::A));
    emulator.run_frame();
    REQUIRE((bus.read16(0x04000084U).value & 0x0001U) != 0U);
    emulator.set_pressed_keys(static_cast<std::uint16_t>(Key::B));
    emulator.run_frame();
    REQUIRE((bus.read16(0x04000084U).value & 0x0008U) != 0U);
    std::vector<std::int16_t> audio;
    emulator.take_audio_samples(audio);
    REQUIRE(std::any_of(audio.begin(), audio.end(),
                        [](const std::int16_t sample) { return sample != 0; }));

    emulator.set_pressed_keys(Key::Left | Key::Down);
    for (int frame = 0; frame < 5; ++frame) {
        emulator.run_frame();
    }
    REQUIRE(ewram(emulator, srgba::homebrew::kTilesDemoBallXAddress) < x);
    REQUIRE(ewram(emulator, srgba::homebrew::kTilesDemoBallYAddress) > y);
    REQUIRE_FALSE(emulator.fault().has_value());
}
