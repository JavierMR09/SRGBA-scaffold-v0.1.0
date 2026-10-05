#include "tiles_demo_rom.hpp"

#include "arm_assembler.hpp"
#include "demo_rom.hpp"

#include <array>
#include <cstddef>

namespace srgba::homebrew {
namespace {

using Op = ArmAssembler::Opcode;
constexpr unsigned kLr = ArmAssembler::kLr;
constexpr std::uint32_t kRomBase = 0x08000000U;
constexpr std::uint32_t kHeaderEnd = 0xC0U;
constexpr std::uint32_t kBallSize = 16;

// DMA3 control: enable, 32-bit, immediate.
constexpr std::uint32_t kDmaCopyWords = 0x84000000U;
// DMA3 control: enable, 32-bit, immediate, fixed source (fill).
constexpr std::uint32_t kDmaFillWords = 0x85000000U;

// r0 must hold 0x04000000. Starts DMA3 from `source` (r1 already holds it) to `destination`.
void emit_dma3_from_r1(ArmAssembler& a, const std::uint32_t destination,
                       const std::uint32_t control) {
    a.str(1, 0, 0xD4);
    a.load_constant(1, destination);
    a.str(1, 0, 0xD8);
    a.load_constant(1, control);
    a.str(1, 0, 0xDC);
}

// Copies 32-bit words from a ROM label.
void emit_dma3(ArmAssembler& a, const std::string& source_label, const std::uint32_t destination,
               const std::uint32_t control) {
    a.load_address(1, source_label);
    emit_dma3_from_r1(a, destination, control);
}

// Fills `words` words with `pattern`. DMA reads from the cartridge always count upwards, so the
// fixed-source pattern is staged in IWRAM first, as games do.
void emit_dma3_fill(ArmAssembler& a, const std::uint32_t pattern, const std::uint32_t destination,
                    const std::uint32_t words) {
    a.load_constant(2, pattern);
    a.mov_imm(1, 0x03000000U);
    a.str(2, 1);
    emit_dma3_from_r1(a, destination, kDmaFillWords | words);
}

[[nodiscard]] std::array<std::uint32_t, 32> ball_tiles() {
    std::array<std::uint32_t, 32> words{};
    for (unsigned y = 0; y < kBallSize; ++y) {
        for (unsigned x = 0; x < kBallSize; ++x) {
            const auto index = tiles_demo_ball_pixel(x, y);
            // 1D mapping: tiles in reading order, 32 bytes each, two pixels per byte.
            const auto tile = (y / 8U) * 2U + x / 8U;
            const auto byte = tile * 32U + (y % 8U) * 4U + (x % 8U) / 2U;
            const auto shift = (byte % 4U) * 8U + (x % 2U) * 4U;
            words[byte / 4U] |= static_cast<std::uint32_t>(index) << shift;
        }
    }
    return words;
}

} // namespace

std::uint8_t tiles_demo_ball_pixel(const unsigned x, const unsigned y) noexcept {
    // Distances are measured in half pixels from the ball's center at (7.5, 7.5).
    const auto dx = static_cast<int>(x * 2U) - 15;
    const auto dy = static_cast<int>(y * 2U) - 15;
    const auto distance = dx * dx + dy * dy;
    if (distance > 15 * 15) {
        return 0;
    }
    if (distance > 12 * 12) {
        return 1;
    }
    const auto hx = static_cast<int>(x * 2U) - 10;
    const auto hy = static_cast<int>(y * 2U) - 10;
    return hx * hx + hy * hy <= 4 * 4 ? 3 : 2;
}

std::vector<std::uint8_t> build_tiles_demo_rom() {
    ArmAssembler a(kRomBase);
    a.b("start");
    while (a.here() < kRomBase + kHeaderEnd) {
        a.word(0);
    }

    a.label("start");
    a.mov_imm(0, 0x04000000U);
    a.add_imm(1, 0, 0x200U);
    a.load_constant(2, 0x4317U);
    a.strh(2, 1, 4); // WAITCNT
    a.mov_imm(1, 0x80U);
    a.strh(1, 0); // DISPCNT: forced blank while loading

    // Palettes, tiles, and sprite graphics are all loaded with DMA channel 3.
    emit_dma3(a, "bg_palette", 0x05000000U, kDmaCopyWords | 2U);
    emit_dma3(a, "obj_palette", 0x05000200U, kDmaCopyWords | 4U);
    emit_dma3_fill(a, 0x11111111U, 0x06000020U, 8U); // tile 1: light
    emit_dma3_fill(a, 0x22222222U, 0x06000040U, 8U); // tile 2: dark
    emit_dma3(a, "ball_tiles", 0x06010000U, kDmaCopyWords | 32U);

    // BG0 map at screen block 31: a checkerboard of tiles 1 and 2.
    a.load_constant(12, 0x0600F800U);
    a.mov_imm(5, 0); // y
    a.label("map_row");
    a.mov_imm(2, 0); // x
    a.label("map_column");
    a.data_reg(Op::Eor, 3, 2, 5);
    a.and_imm(3, 3, 1);
    a.add_imm(3, 3, 1);
    a.strh_post(3, 12, 2);
    a.add_imm(2, 2, 1);
    a.cmp_imm(2, 32);
    a.b("map_column", Condition::NotEqual);
    a.add_imm(5, 5, 1);
    a.cmp_imm(5, 32);
    a.b("map_row", Condition::NotEqual);

    // Hide every sprite (affine off, "disable" bit set).
    a.mov_imm(12, 0x07000000U);
    a.mov_imm(1, 0x0200U);
    a.mov_imm(2, 128);
    a.label("hide_sprites");
    a.strh(1, 12, 0);
    a.add_imm(12, 12, 8);
    a.sub_imm(2, 2, 1, Condition::Always, true);
    a.b("hide_sprites", Condition::NotEqual);

    a.load_constant(1, 0x1F01U); // BG0CNT: screen block 31, priority 1
    a.strh(1, 0, 0x08);
    a.load_constant(1, 0x0181U); // BLDCNT: brighten BG0; BG0 is also the 2nd target
    a.strh(1, 0, 0x50);
    a.load_constant(1, 0x0808U); // BLDALPHA: 8/16 + 8/16 for the shadow
    a.strh(1, 0, 0x52);
    a.mov_imm(1, 6); // BLDY
    a.strh(1, 0, 0x54);
    a.mov_imm(1, 0xA0U); // WIN0V: lines 0-159
    a.strh(1, 0, 0x44);
    a.mov_imm(1, 0x3FU); // WININ: everything, with effects
    a.strh(1, 0, 0x48);
    a.mov_imm(1, 0x1FU); // WINOUT: everything, no effects
    a.strh(1, 0, 0x4A);

    a.add_imm(1, 0, 0x100U);
    a.mov_imm(2, 0x83U); // TM2CNT: start, prescaler /1024 (one sweep every ~4 s)
    a.strh(2, 1, 0x0A);

    a.load_address(1, "irq_handler");
    a.str(1, 0, -4);
    a.mov_imm(1, 0x0008U);
    a.strh(1, 0, 4); // DISPSTAT: VBlank IRQ
    a.add_imm(2, 0, 0x200U);
    a.mov_imm(1, 1);
    a.strh(1, 2, 0); // IE = VBlank
    a.strh(1, 2, 8); // IME = 1

    a.mov_imm(4, kTilesDemoStartX);
    a.mov_imm(5, kTilesDemoStartY);
    a.mov_imm(6, 0);             // scroll position
    a.load_constant(1, 0x3140U); // DISPCNT: mode 0, BG0, OBJ (1D), window 0
    a.strh(1, 0);

    a.label("main_loop");
    a.swi(0x05); // VBlankIntrWait
    a.mov_imm(0, 0x04000000U);

    // D-pad moves the ball two pixels per frame.
    a.add_imm(1, 0, 0x100U);
    a.ldrh(1, 1, 0x30); // KEYINPUT
    a.tst_imm(1, 0x10U);
    a.add_imm(4, 4, 2, Condition::Equal);
    a.tst_imm(1, 0x20U);
    a.sub_imm(4, 4, 2, Condition::Equal);
    a.tst_imm(1, 0x40U);
    a.sub_imm(5, 5, 2, Condition::Equal);
    a.tst_imm(1, 0x80U);
    a.add_imm(5, 5, 2, Condition::Equal);
    a.cmp_imm(4, 0);
    a.mov_imm(4, 0, Condition::LessThan);
    a.cmp_imm(4, 240 - kBallSize);
    a.mov_imm(4, 240 - kBallSize, Condition::GreaterThan);
    a.cmp_imm(5, 0);
    a.mov_imm(5, 0, Condition::LessThan);
    a.cmp_imm(5, 160 - kBallSize);
    a.mov_imm(5, 160 - kBallSize, Condition::GreaterThan);

    // Scroll the checkerboard diagonally.
    a.add_imm(6, 6, 1);
    a.strh(6, 0, 0x10); // BG0HOFS
    a.strh(6, 0, 0x12); // BG0VOFS

    // Highlight band: left edge = timer 2 counter / 256, 40 pixels wide, clipped at 240.
    a.add_imm(1, 0, 0x100U);
    a.ldrh(1, 1, 0x08); // TM2CNT_L
    a.mov(1, 1, ShiftType::LogicalRight, 8);
    a.add_imm(2, 1, 40);
    a.cmp_imm(2, 240);
    a.mov_imm(2, 240, Condition::GreaterThan);
    a.cmp_imm(1, 240);
    a.mov_imm(1, 240, Condition::GreaterThan);
    a.data_reg(Op::Orr, 1, 2, 1, ShiftType::LogicalLeft, 8);
    a.strh(1, 0, 0x40); // WIN0H

    // Sprite 0: the ball (16x16, priority 0). Sprite 1: its semi-transparent shadow.
    a.mov_imm(12, 0x07000000U);
    a.strh(5, 12, 0);
    a.orr_imm(1, 4, 0x4000U);
    a.strh(1, 12, 2);
    a.mov_imm(1, 0);
    a.strh(1, 12, 4);
    a.add_imm(1, 5, kTilesDemoShadowOffset);
    a.orr_imm(1, 1, 0x0400U); // semi-transparent
    a.strh(1, 12, 8);
    a.add_imm(1, 4, kTilesDemoShadowOffset);
    a.orr_imm(1, 1, 0x4000U);
    a.strh(1, 12, 10);
    a.load_constant(1, 0x1400U); // palette bank 1 (black), priority 1
    a.strh(1, 12, 12);

    a.mov_imm(1, 0x02000000U);
    a.str(4, 1, 4);
    a.str(5, 1, 8);
    a.b("main_loop");
    a.literal_pool();

    a.label("irq_handler");
    a.mov_imm(0, 0x04000000U);
    a.ldr(1, 0, 0x200);
    a.data_reg(Op::And, 1, 1, 1, ShiftType::LogicalRight, 16);
    a.add_imm(3, 0, 0x200U);
    a.strh(1, 3, 2);
    a.ldrh(2, 0, -8);
    a.data_reg(Op::Orr, 2, 2, 1);
    a.strh(2, 0, -8);
    a.mov_imm(3, kTilesDemoFrameCounterAddress);
    a.ldr(2, 3);
    a.add_imm(2, 2, 1);
    a.str(2, 3);
    a.bx(kLr);

    // Data.
    a.label("bg_palette");
    a.word(static_cast<std::uint32_t>(kTilesDemoLightTile) << 16U); // 0: backdrop, 1: light
    a.word(kTilesDemoDarkTile);                                     // 2: dark
    a.label("obj_palette");
    a.word(static_cast<std::uint32_t>(kTilesDemoBallOutline) << 16U);
    a.word(kTilesDemoBallFill | (static_cast<std::uint32_t>(kTilesDemoBallHighlight) << 16U));
    a.word(0); // bank 0, colors 4-7
    a.word(0);
    a.label("ball_tiles");
    for (const auto word : ball_tiles()) {
        a.word(word);
    }

    auto rom = a.finish();
    write_cartridge_header(rom, "SRGBA TILES", "SRGT");
    rom.resize((rom.size() + 1023U) & ~std::size_t{1023U}, 0xFFU);
    return rom;
}

} // namespace srgba::homebrew
