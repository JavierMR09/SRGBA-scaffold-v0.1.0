#include "test_helpers.hpp"

#include "srgba/core/emulator.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/ppu.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using srgba::core::Emulator;
using srgba::core::Framebuffer;
using srgba::core::GbaBus;
using srgba::core::Ppu;
using srgba::core::Rgba8;
using srgba::core::Scheduler;

namespace {

constexpr Rgba8 kWhite{255, 255, 255, 255};

// A bus in the post-BIOS state (identity affine parameters for BG2/BG3).
struct VideoFixture {
    VideoFixture() {
        bus.initialize_post_bios();
    }

    Framebuffer& render() {
        ppu.render_frame(bus, framebuffer);
        return framebuffer;
    }

    [[nodiscard]] Rgba8 pixel(const std::size_t x, const std::size_t y) const {
        return framebuffer[y * 240U + x];
    }

    GbaBus bus;
    Ppu ppu;
    Framebuffer framebuffer{};
};

void run_cycles(Emulator& emulator, const std::uint64_t cycles) {
    const auto target = emulator.cycle_counter() + cycles;
    while (emulator.cycle_counter() < target) {
        REQUIRE(emulator.step_instruction().has_value());
    }
}

} // namespace

TEST_CASE("BGR555 colors expand to full-range RGBA", "[ppu]") {
    REQUIRE(Ppu::to_rgba(0x7FFFU) == kWhite);
    REQUIRE(Ppu::to_rgba(0x001FU) == Rgba8{255, 0, 0, 255});
    REQUIRE(Ppu::to_rgba(0x03E0U) == Rgba8{0, 255, 0, 255});
    REQUIRE(Ppu::to_rgba(0x0000U) == Rgba8{0, 0, 0, 255});
}

TEST_CASE("Display timing drives VCOUNT, DISPSTAT flags, and video interrupts", "[ppu][timing]") {
    const srgba::tests::TemporaryRom rom(srgba::tests::make_m2_cpu_test_rom());
    Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));
    auto& bus = emulator.bus();

    // Enable VBlank, HBlank, and VCount(=100) interrupt requests.
    static_cast<void>(bus.write16(0x04000004U, 0x6438U));
    REQUIRE(bus.read16(0x04000006U).value == 0U);

    run_cycles(emulator, Ppu::kHDrawCycles + 8U);
    REQUIRE((bus.read16(0x04000004U).value & 0x2U) != 0U);
    REQUIRE((bus.interrupt_flags() & 0x2U) != 0U);

    run_cycles(emulator, Ppu::kCyclesPerLine * 100U - Ppu::kHDrawCycles);
    REQUIRE(bus.read16(0x04000006U).value == 100U);
    REQUIRE((bus.read16(0x04000004U).value & 0x4U) != 0U);
    REQUIRE((bus.interrupt_flags() & 0x4U) != 0U);
    REQUIRE((bus.interrupt_flags() & 0x1U) == 0U);

    run_cycles(emulator, Ppu::kCyclesPerLine * 60U);
    REQUIRE(bus.read16(0x04000006U).value == 160U);
    REQUIRE((bus.read16(0x04000004U).value & 0x1U) != 0U);
    REQUIRE((bus.interrupt_flags() & 0x1U) != 0U);

    // Line 227 is inside the vertical blank period but no longer reports the VBlank flag.
    run_cycles(emulator, Ppu::kCyclesPerLine * 67U);
    REQUIRE(bus.read16(0x04000006U).value == 227U);
    REQUIRE((bus.read16(0x04000004U).value & 0x1U) == 0U);
}

TEST_CASE("A frame lasts exactly 280896 master cycles", "[ppu][timing]") {
    const srgba::tests::TemporaryRom rom(srgba::tests::make_m2_cpu_test_rom());
    Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));

    emulator.run_frame();
    REQUIRE(emulator.frame_counter() == 1U);
    REQUIRE(emulator.cycle_counter() >= Ppu::kCyclesPerFrame);
    // A frame ends at the first instruction boundary at or after the 280,896-cycle mark.
    REQUIRE(emulator.cycle_counter() < Ppu::kCyclesPerFrame + 32U);
    REQUIRE(emulator.ppu().vcount() == 0U);

    emulator.run_frame();
    REQUIRE(emulator.frame_counter() == 2U);
    REQUIRE(emulator.cycle_counter() >= 2U * Ppu::kCyclesPerFrame);
}

TEST_CASE("Forced blank renders white scanlines", "[ppu][render]") {
    VideoFixture video;
    static_cast<void>(video.bus.write16(0x04000000U, 0x0080U));
    video.render();
    REQUIRE(video.pixel(0, 10) == kWhite);
    REQUIRE(video.pixel(239, 10) == kWhite);
}

TEST_CASE("Mode 3 renders direct 15-bit color from VRAM", "[ppu][render]") {
    VideoFixture video;
    static_cast<void>(video.bus.write16(0x04000000U, 0x0403U));
    static_cast<void>(video.bus.write16(0x06000000U + (5U * 240U + 7U) * 2U, 0x001FU));
    video.render();
    REQUIRE(video.pixel(7, 5) == Rgba8{255, 0, 0, 255});
    REQUIRE(video.pixel(8, 5) == Rgba8{0, 0, 0, 255});
}

TEST_CASE("Mode 4 renders paletted pixels from the selected page", "[ppu][render]") {
    VideoFixture video;
    auto& bus = video.bus;
    static_cast<void>(bus.write16(0x05000000U, 0x7C00U)); // backdrop: blue
    static_cast<void>(bus.write16(0x05000006U, 0x03E0U)); // palette 3: green
    static_cast<void>(bus.write16(0x06000000U, 0x0300U)); // page 0: pixel 1 = index 3
    static_cast<void>(bus.write16(0x0600A000U, 0x0003U)); // page 1: pixel 0 = index 3

    static_cast<void>(bus.write16(0x04000000U, 0x0404U));
    video.render();
    REQUIRE(video.pixel(0, 0) == Rgba8{0, 0, 255, 255}); // index 0 shows the backdrop
    REQUIRE(video.pixel(1, 0) == Rgba8{0, 255, 0, 255});

    static_cast<void>(bus.write16(0x04000000U, 0x0414U)); // frame select
    video.render();
    REQUIRE(video.pixel(0, 0) == Rgba8{0, 255, 0, 255});
    REQUIRE(video.pixel(1, 0) == Rgba8{0, 0, 255, 255});
}

TEST_CASE("Mode 5 renders a 160x128 bitmap surrounded by the backdrop", "[ppu][render]") {
    VideoFixture video;
    auto& bus = video.bus;
    static_cast<void>(bus.write16(0x05000000U, 0x001FU));
    static_cast<void>(bus.write16(0x06000000U + (2U * 160U + 159U) * 2U, 0x7FFFU));
    static_cast<void>(bus.write16(0x04000000U, 0x0405U));
    video.render();
    REQUIRE(video.pixel(159, 2) == kWhite);
    REQUIRE(video.pixel(160, 2) == Rgba8{255, 0, 0, 255});
    REQUIRE(video.pixel(0, 130) == Rgba8{255, 0, 0, 255});
}

TEST_CASE("Disabling BG2 in a bitmap mode shows the backdrop color", "[ppu][render]") {
    VideoFixture video;
    static_cast<void>(video.bus.write16(0x05000000U, 0x03E0U));
    static_cast<void>(video.bus.write16(0x06000000U, 0x7FFFU));
    static_cast<void>(video.bus.write16(0x04000000U, 0x0003U));
    video.render();
    REQUIRE(video.pixel(0, 0) == Rgba8{0, 255, 0, 255});
}
