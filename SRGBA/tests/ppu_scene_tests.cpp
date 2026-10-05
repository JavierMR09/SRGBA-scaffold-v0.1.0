#include "srgba/core/gba_bus.hpp"
#include "srgba/core/ppu.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

namespace {

using srgba::core::Framebuffer;
using srgba::core::GbaBus;
using srgba::core::Ppu;
using srgba::core::Rgba8;

constexpr std::uint16_t kRed = 0x001FU;
constexpr std::uint16_t kGreen = 0x03E0U;
constexpr std::uint16_t kBlue = 0x7C00U;
constexpr std::uint16_t kWhite = 0x7FFFU;
constexpr std::uint16_t kGray = 0x4210U; // 16, 16, 16

// Builds small video scenes directly in VRAM, palette RAM, and OAM.
class Scene {
  public:
    Scene() {
        bus.initialize_post_bios();
        write16(0x000, 0x0000); // leave forced blank
    }

    void write16(const std::uint32_t io_offset, const std::uint16_t value) {
        static_cast<void>(bus.write16(0x04000000U + io_offset, value));
    }
    void write32(const std::uint32_t io_offset, const std::uint32_t value) {
        static_cast<void>(bus.write32(0x04000000U + io_offset, value));
    }
    void background_color(const std::size_t index, const std::uint16_t color) {
        static_cast<void>(bus.write16(0x05000000U + static_cast<std::uint32_t>(index) * 2U, color));
    }
    void object_color(const std::size_t index, const std::uint16_t color) {
        static_cast<void>(bus.write16(0x05000200U + static_cast<std::uint32_t>(index) * 2U, color));
    }
    // Fills a 4bpp tile with one palette index.
    void solid_tile4(const std::uint32_t vram_offset, const std::size_t tile,
                     const std::uint8_t index) {
        const auto packed = static_cast<std::uint16_t>(index * 0x1111U);
        for (std::uint32_t byte = 0; byte < 32U; byte += 2U) {
            static_cast<void>(bus.write16(
                0x06000000U + vram_offset + static_cast<std::uint32_t>(tile) * 32U + byte, packed));
        }
    }
    void map_entry(const std::uint32_t screen_base, const std::size_t tile_x,
                   const std::size_t tile_y, const std::uint16_t entry) {
        static_cast<void>(bus.write16(0x06000000U + screen_base +
                                          static_cast<std::uint32_t>(tile_y * 32U + tile_x) * 2U,
                                      entry));
    }
    void fill_map(const std::uint32_t screen_base, const std::uint16_t entry) {
        for (std::size_t y = 0; y < 32U; ++y) {
            for (std::size_t x = 0; x < 32U; ++x) {
                map_entry(screen_base, x, y, entry);
            }
        }
    }
    void sprite(const std::size_t index, const std::uint16_t a0, const std::uint16_t a1,
                const std::uint16_t a2) {
        const auto base = 0x07000000U + static_cast<std::uint32_t>(index) * 8U;
        static_cast<void>(bus.write16(base, a0));
        static_cast<void>(bus.write16(base + 2U, a1));
        static_cast<void>(bus.write16(base + 4U, a2));
    }
    void hide_all_sprites() {
        for (std::size_t index = 0; index < 128U; ++index) {
            sprite(index, 0x0200U, 0, 0);
        }
    }
    void render() {
        ppu.render_frame(bus, framebuffer);
    }
    [[nodiscard]] Rgba8 at(const std::size_t x, const std::size_t y) const {
        return framebuffer[y * 240U + x];
    }

    GbaBus bus;
    Ppu ppu;
    Framebuffer framebuffer{};
};

[[nodiscard]] Rgba8 rgb(const std::uint16_t color) {
    return Ppu::to_rgba(color);
}

// BG0: 4bpp tiles at char base 0, map at screen base 31 (0xF800).
void setup_text_background(Scene& scene, const std::uint16_t control_extra = 0) {
    scene.background_color(0, kBlue); // backdrop
    scene.background_color(1, kRed);
    scene.background_color(17, kGreen); // palette bank 1, index 1
    scene.solid_tile4(0, 1, 1);
    scene.fill_map(0xF800U, 0x0000U); // tile 0 is transparent
    scene.write16(0x008, static_cast<std::uint16_t>(0x1F00U | control_extra));
}

} // namespace

TEST_CASE("Text backgrounds draw 4bpp tiles with palette banks and flips", "[ppu][tiles]") {
    Scene scene;
    setup_text_background(scene);
    // Tile 2: left column index 1, rest transparent, to observe horizontal flips.
    for (std::uint32_t row = 0; row < 8U; ++row) {
        static_cast<void>(scene.bus.write8(0x06000040U + row * 4U, 0x01U));
    }
    scene.map_entry(0xF800U, 0, 0, 0x0001U); // tile 1, bank 0
    scene.map_entry(0xF800U, 1, 0, 0x1001U); // tile 1, bank 1
    scene.map_entry(0xF800U, 2, 0, 0x0002U); // tile 2
    scene.map_entry(0xF800U, 3, 0, 0x0402U); // tile 2, horizontal flip
    scene.write16(0x000, 0x0100);            // mode 0, BG0
    scene.render();

    REQUIRE(scene.at(0, 0) == rgb(kRed));
    REQUIRE(scene.at(8, 7) == rgb(kGreen));
    REQUIRE(scene.at(16, 0) == rgb(kRed));
    REQUIRE(scene.at(17, 0) == rgb(kBlue));
    REQUIRE(scene.at(31, 0) == rgb(kRed));
    REQUIRE(scene.at(30, 0) == rgb(kBlue));
    REQUIRE(scene.at(0, 8) == rgb(kBlue)); // transparent tile shows the backdrop
}

TEST_CASE("Text background scrolling wraps around the map", "[ppu][tiles]") {
    Scene scene;
    setup_text_background(scene);
    scene.map_entry(0xF800U, 0, 0, 0x0001U);
    scene.write16(0x010, 252); // BG0HOFS
    scene.write16(0x012, 4);   // BG0VOFS
    scene.write16(0x000, 0x0100);
    scene.render();
    // Map pixel (0..7, 4..7) appears at screen x 4..11, y 0..3.
    REQUIRE(scene.at(3, 0) == rgb(kBlue));
    REQUIRE(scene.at(4, 0) == rgb(kRed));
    REQUIRE(scene.at(11, 3) == rgb(kRed));
    REQUIRE(scene.at(4, 4) == rgb(kBlue));
}

TEST_CASE("Wide text backgrounds use the second screen block", "[ppu][tiles]") {
    Scene scene;
    scene.background_color(0, kBlue);
    scene.background_color(1, kRed);
    scene.solid_tile4(0, 1, 1);
    for (std::uint32_t block = 0; block < 2U; ++block) {
        for (std::size_t y = 0; y < 32U; ++y) {
            for (std::size_t x = 0; x < 32U; ++x) {
                scene.map_entry(0xE000U + block * 0x800U, x, y, 0);
            }
        }
    }
    scene.map_entry(0xE800U, 0, 0, 0x0001U); // first tile of the right-hand block
    scene.write16(0x008, 0x5C00U);           // screen base 28, 512x256
    scene.write16(0x010, 256);
    scene.write16(0x000, 0x0100);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kRed));
    REQUIRE(scene.at(8, 0) == rgb(kBlue));
}

TEST_CASE("8bpp text tiles index the full palette", "[ppu][tiles]") {
    Scene scene;
    scene.background_color(0, kBlue);
    scene.background_color(200, kWhite);
    for (std::uint32_t byte = 0; byte < 64U; byte += 2U) {
        static_cast<void>(scene.bus.write16(0x06000040U + byte, 0xC8C8U)); // tile 1 (64 bytes)
    }
    scene.fill_map(0xF800U, 0);
    scene.map_entry(0xF800U, 1, 1, 0x0001U);
    scene.write16(0x008, 0x1F80U);
    scene.write16(0x000, 0x0100);
    scene.render();
    REQUIRE(scene.at(8, 8) == rgb(kWhite));
    REQUIRE(scene.at(7, 8) == rgb(kBlue));
}

TEST_CASE("Lower priority numbers and lower background numbers draw in front", "[ppu][priority]") {
    Scene scene;
    setup_text_background(scene, 0x0001U); // BG0 priority 1
    scene.map_entry(0xF800U, 0, 0, 0x0001U);
    scene.background_color(2, kWhite);
    scene.solid_tile4(0, 2, 2);
    for (std::size_t y = 0; y < 32U; ++y) {
        for (std::size_t x = 0; x < 32U; ++x) {
            scene.map_entry(0xF000U, x, y, 0x0002U);
        }
    }
    scene.write16(0x00A, 0x1E00U); // BG1 priority 0, screen base 30
    scene.write16(0x000, 0x0300);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kWhite));

    scene.write16(0x00A, 0x1E01U); // equal priority: BG0 wins
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kRed));
    REQUIRE(scene.at(100, 100) == rgb(kWhite));
}

TEST_CASE("Affine backgrounds scale, wrap, and clip", "[ppu][affine]") {
    Scene scene;
    scene.background_color(0, kBlue);
    scene.background_color(5, kGreen);
    for (std::uint32_t byte = 0; byte < 64U; byte += 2U) {
        static_cast<void>(scene.bus.write16(0x06000040U + byte, 0x0505U)); // 8bpp tile 1
    }
    // 128x128 map (16x16 bytes) at screen base 31; only map cell (0,0) uses tile 1.
    for (std::uint32_t cell = 0; cell < 256U; cell += 2U) {
        static_cast<void>(scene.bus.write16(0x0600F800U + cell, 0));
    }
    static_cast<void>(scene.bus.write16(0x0600F800U, 0x0001U));
    scene.write16(0x00C, 0x1F00U); // BG2: no wrap, 128x128
    scene.write16(0x020, 0x0080);  // PA = 0.5: 2x horizontal zoom
    scene.write16(0x000, 0x0402);  // mode 2, BG2
    scene.render();
    REQUIRE(scene.at(15, 0) == rgb(kGreen));
    REQUIRE(scene.at(16, 0) == rgb(kBlue));
    REQUIRE(scene.at(0, 8) == rgb(kBlue));
    REQUIRE(scene.at(0, 130) == rgb(kBlue)); // outside the 128x128 map

    scene.write16(0x00C, 0x3F00U); // wrap
    scene.render();
    REQUIRE(scene.at(0, 128) == rgb(kGreen));
    REQUIRE(scene.at(0, 136) == rgb(kBlue));
}

TEST_CASE("Writing BG2X moves the bitmap and reloads the reference point", "[ppu][affine]") {
    Scene scene;
    static_cast<void>(scene.bus.write16(0x06000000U + (0U * 240U + 10U) * 2U, kRed));
    scene.write32(0x028, 10U << 8U); // BG2X = 10.0
    scene.write16(0x000, 0x0403);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kRed));
    REQUIRE(scene.at(10, 0) == rgb(0));
    REQUIRE(scene.at(235, 0) == rgb(0)); // beyond the bitmap: backdrop (black)

    // PD = 2 (2x vertical shrink): line 1 samples bitmap row 2.
    static_cast<void>(scene.bus.write16(0x06000000U + (2U * 240U + 10U) * 2U, kGreen));
    scene.write16(0x026, 0x0200);
    scene.render();
    REQUIRE(scene.at(0, 1) == rgb(kGreen));
}

TEST_CASE("Sprites draw with 1D and 2D tile mapping and flips", "[ppu][sprites]") {
    Scene scene;
    scene.hide_all_sprites();
    scene.background_color(0, kBlue);
    scene.object_color(1, kRed);
    scene.object_color(2, kGreen);
    // OBJ tiles 0 and 1: tile 0 = index 1, tile 1 = index 2, tile 32 = index 2.
    scene.solid_tile4(0x10000U, 0, 1);
    scene.solid_tile4(0x10000U, 1, 2);
    scene.solid_tile4(0x10000U, 32, 2);
    // 16x16 square sprite at (40, 30), 4bpp, tile 0.
    scene.sprite(0, 30, 0x4000U | 40U, 0);
    scene.write16(0x000, 0x1040); // OBJ on, 1D mapping
    scene.render();
    REQUIRE(scene.at(40, 30) == rgb(kRed));
    REQUIRE(scene.at(48, 30) == rgb(kGreen)); // next tile in 1D
    REQUIRE(scene.at(40, 38) == rgb(kBlue));  // tile 2 is empty in 1D mapping
    REQUIRE(scene.at(39, 30) == rgb(kBlue));

    scene.write16(0x000, 0x1000); // 2D mapping: the second row starts 32 tiles later
    scene.render();
    REQUIRE(scene.at(40, 38) == rgb(kGreen));

    scene.sprite(0, 30, 0x5000U | 40U, 0); // horizontal flip
    scene.render();
    REQUIRE(scene.at(40, 30) == rgb(kGreen));
    REQUIRE(scene.at(55, 30) == rgb(kRed));
}

TEST_CASE("Sprite priority, OAM order, and disabled sprites", "[ppu][sprites][priority]") {
    Scene scene;
    scene.hide_all_sprites();
    setup_text_background(scene, 0x0001U); // BG0 priority 1
    scene.map_entry(0xF800U, 0, 0, 0x0001U);
    scene.object_color(1, kWhite);
    scene.object_color(2, kGreen);
    scene.solid_tile4(0x10000U, 0, 1);
    scene.solid_tile4(0x10000U, 1, 2);

    scene.sprite(0, 0, 0, 0x0800U); // priority 2: behind BG0
    scene.write16(0x000, 0x1140);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kRed));

    scene.sprite(0, 0, 0, 0x0400U); // priority 1: in front of BG0 (same priority)
    scene.sprite(1, 0, 4, 0x0401U); // later OAM entry, same priority, tile 1
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kWhite));
    REQUIRE(scene.at(4, 0) == rgb(kWhite)); // lower OAM index wins
    REQUIRE(scene.at(11, 0) == rgb(kGreen));

    scene.sprite(0, 0x0200U, 0, 0x0400U); // disabled (double-size bit without affine)
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kRed));
    REQUIRE(scene.at(4, 0) == rgb(kGreen));
}

TEST_CASE("Sprites wrap vertically and horizontally at the screen edges", "[ppu][sprites]") {
    Scene scene;
    scene.hide_all_sprites();
    scene.object_color(1, kRed);
    scene.solid_tile4(0x10000U, 0, 1);
    scene.sprite(0, 252, 508, 0); // y = -4, x = -4
    scene.write16(0x000, 0x1040);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kRed));
    REQUIRE(scene.at(3, 3) == rgb(kRed));
    REQUIRE(scene.at(4, 0) == rgb(0));
    REQUIRE(scene.at(0, 4) == rgb(0));
}

TEST_CASE("Affine sprites rotate and double-size sprites grow their bounding box",
          "[ppu][sprites][affine]") {
    Scene scene;
    scene.hide_all_sprites();
    scene.object_color(1, kRed);
    scene.solid_tile4(0x10000U, 0, 1);
    // Affine group 0: identity scaled 0.5 (PA = PD = 0x80) => 2x magnification.
    static_cast<void>(scene.bus.write16(0x07000006U, 0x0080U));
    static_cast<void>(scene.bus.write16(0x0700000EU, 0x0000U));
    static_cast<void>(scene.bus.write16(0x07000016U, 0x0000U));
    static_cast<void>(scene.bus.write16(0x0700001EU, 0x0080U));
    scene.sprite(0, 0x0300U | 50U, 60U, 0); // affine + double size, 8x8 at (60, 50)
    scene.write16(0x000, 0x1040);
    scene.render();
    // The 8x8 sprite magnified 2x fills the whole 16x16 double-size box.
    REQUIRE(scene.at(60, 50) == rgb(kRed));
    REQUIRE(scene.at(75, 65) == rgb(kRed));
    REQUIRE(scene.at(76, 66) == rgb(0));

    // Without double size the box is 8x8 and shows the magnified center.
    scene.sprite(0, 0x0100U | 50U, 60U, 0);
    scene.render();
    REQUIRE(scene.at(60, 50) == rgb(kRed));
    REQUIRE(scene.at(68, 50) == rgb(0));
}

TEST_CASE("Window 0 masks layers and the object window applies its own controls",
          "[ppu][windows]") {
    Scene scene;
    scene.hide_all_sprites();
    setup_text_background(scene);
    scene.fill_map(0xF800U, 0x0001U);        // BG0 is solid red
    scene.write16(0x040, (10U << 8U) | 20U); // WIN0H: x 10..19
    scene.write16(0x044, (5U << 8U) | 15U);  // WIN0V: y 5..14
    scene.write16(0x048, 0x0000);            // inside WIN0: nothing
    scene.write16(0x04A, 0x0001);            // outside: BG0
    scene.write16(0x000, 0x2100);
    scene.render();
    REQUIRE(scene.at(10, 5) == rgb(kBlue));
    REQUIRE(scene.at(19, 14) == rgb(kBlue));
    REQUIRE(scene.at(20, 5) == rgb(kRed));
    REQUIRE(scene.at(10, 15) == rgb(kRed));

    // Object window: a sprite in OBJ-window mode reveals BG0 only where it is opaque.
    scene.object_color(1, kWhite);
    scene.solid_tile4(0x10000U, 0, 1);
    scene.sprite(0, 0x0800U | 100U, 100U, 0); // mode 2: OBJ window
    scene.write16(0x04A, 0x0100);             // outside: nothing; OBJ window: BG0
    scene.write16(0x000, 0x9140);
    scene.render();
    REQUIRE(scene.at(100, 100) == rgb(kRed));
    REQUIRE(scene.at(108, 100) == rgb(kBlue));
}

TEST_CASE("Color special effects blend, brighten, and darken", "[ppu][blending]") {
    Scene scene;
    scene.hide_all_sprites();
    setup_text_background(scene);
    scene.fill_map(0xF800U, 0x0001U); // BG0 red
    scene.background_color(2, kBlue);
    scene.solid_tile4(0, 2, 2);
    for (std::size_t y = 0; y < 32U; ++y) {
        for (std::size_t x = 0; x < 32U; ++x) {
            scene.map_entry(0xF000U, x, y, 0x0002U);
        }
    }
    scene.write16(0x00A, 0x1E01U); // BG1 blue, priority 1 (behind BG0)
    scene.write16(0x000, 0x0300);

    scene.write16(0x050, 0x0241); // alpha: first BG0, second BG1
    scene.write16(0x052, 0x0808); // EVA = EVB = 8/16
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(0x3C0FU)); // half red + half blue

    scene.write16(0x050, 0x0081); // brighten BG0
    scene.write16(0x054, 16);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kWhite));

    scene.write16(0x050, 0x00C1); // darken BG0
    scene.write16(0x054, 8);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(0x0010U)); // 31 - (31 * 8 >> 4)

    // A window without the effect bit disables special effects.
    scene.write16(0x040, 0x00F0); // WIN0H: 0..239
    scene.write16(0x044, 0x00A0); // WIN0V: 0..159
    scene.write16(0x048, 0x0003); // BG0, BG1, no effects
    scene.write16(0x000, 0x2300);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(kRed));
}

TEST_CASE("Semi-transparent sprites blend onto second-target layers", "[ppu][blending][sprites]") {
    Scene scene;
    scene.hide_all_sprites();
    scene.background_color(0, kGray);
    scene.object_color(1, kWhite);
    scene.solid_tile4(0x10000U, 0, 1);
    scene.sprite(0, 0x0400U, 0, 0); // semi-transparent
    scene.write16(0x050, 0x2000);   // second target: backdrop; no effect selected
    scene.write16(0x052, 0x0808);
    scene.write16(0x000, 0x1040);
    scene.render();
    REQUIRE(scene.at(0, 0) == rgb(0x5EF7U)); // (31 + 16) / 2 = 23 per channel
}

TEST_CASE("Background mosaic repeats pixels in blocks", "[ppu][mosaic]") {
    Scene scene;
    scene.background_color(0, kBlue);
    scene.background_color(1, kRed);
    // Tile 1: only pixel (0, 0) is opaque.
    static_cast<void>(scene.bus.write8(0x06000020U, 0x01U));
    scene.fill_map(0xF800U, 0x0001U);
    scene.write16(0x008, 0x1F40U); // mosaic on
    scene.write16(0x04C, 0x0033);  // 4x4 blocks
    scene.write16(0x000, 0x0100);
    scene.render();
    REQUIRE(scene.at(3, 3) == rgb(kRed));
    REQUIRE(scene.at(4, 0) == rgb(kBlue));
    REQUIRE(scene.at(8, 8) == rgb(kRed));
}
