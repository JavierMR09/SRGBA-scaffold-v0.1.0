#include "demo_rom.hpp"

#include "arm_assembler.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace srgba::homebrew {
namespace {

using Op = ArmAssembler::Opcode;
constexpr unsigned kSp = ArmAssembler::kSp;
constexpr unsigned kLr = ArmAssembler::kLr;
constexpr unsigned kPc = ArmAssembler::kPc;
constexpr std::uint32_t kRomBase = 0x08000000U;
constexpr std::size_t kHeaderEnd = 0xC0U;

// r7 = row color component for line r5: ((r5 * 205) >> 10) << 10 | 4
void emit_row_color(ArmAssembler& assembler) {
    assembler.mov_imm(3, 205);
    assembler.mul(7, 5, 3);
    assembler.mov(7, 7, ShiftType::LogicalRight, 10);
    assembler.mov(7, 7, ShiftType::LogicalLeft, 10);
    assembler.orr_imm(7, 7, 4);
}

// r1 = r7 | ((r2 >> 3) << 5); store at [r12], r12 += 2
void emit_background_pixel(ArmAssembler& assembler) {
    assembler.mov(1, 2, ShiftType::LogicalRight, 3);
    assembler.data_reg(Op::Orr, 1, 7, 1, ShiftType::LogicalLeft, 5);
    assembler.strh_post(1, 12, 2);
}

// r12 = VRAM address of pixel (r0, r1); clobbers r3.
void emit_pixel_address(ArmAssembler& assembler) {
    assembler.mov_imm(3, 240);
    assembler.mul(12, 1, 3);
    assembler.add(12, 12, 0);
    assembler.mov_imm(3, 0x06000000U);
    assembler.add(12, 3, 12, ShiftType::LogicalLeft, 1);
}

} // namespace

void write_cartridge_header(std::vector<std::uint8_t>& rom, const char* title,
                            const char* game_code) {
    if (rom.size() < kHeaderEnd) {
        rom.resize(kHeaderEnd, 0);
    }
    std::memset(rom.data() + 0xA0, 0, 0x20);
    std::memcpy(rom.data() + 0xA0, title, std::min<std::size_t>(std::strlen(title), 12U));
    std::memcpy(rom.data() + 0xAC, game_code, std::min<std::size_t>(std::strlen(game_code), 4U));
    rom[0xB0] = '0';
    rom[0xB1] = '1';
    rom[0xB2] = 0x96U;
    std::uint8_t checksum = 0;
    for (std::size_t offset = 0xA0; offset <= 0xBC; ++offset) {
        checksum = static_cast<std::uint8_t>(checksum - rom[offset]);
    }
    rom[0xBD] = static_cast<std::uint8_t>(checksum - 0x19U);
}

std::vector<std::uint8_t> build_demo_rom() {
    ArmAssembler a(kRomBase);

    // Cartridge entry point, then room for the header.
    a.b("start");
    while (a.here() < kRomBase + kHeaderEnd) {
        a.word(0);
    }

    a.label("start");
    a.mov_imm(0, 0x04000000U);
    a.add_imm(1, 0, 0x200U);
    a.load_constant(2, 0x4317U); // WAITCNT: 3/1 ROM wait states with prefetch, like most games
    a.strh(2, 1, 4);
    a.load_constant(1, 0x0483U); // DISPCNT: mode 3, BG2 on, forced blank while drawing
    a.strh(1, 0);
    a.bl("draw_background");

    a.mov_imm(0, 0x04000000U);
    a.load_constant(1, 0x0403U); // DISPCNT: mode 3, BG2 on
    a.strh(1, 0);
    a.load_address(1, "irq_handler");
    a.str(1, 0, -4); // 0x03FFFFFC mirrors the user IRQ vector at 0x03007FFC
    a.mov_imm(1, 0x0008U);
    a.strh(1, 0, 4); // DISPSTAT: VBlank IRQ enable
    a.add_imm(2, 0, 0x200U);
    a.mov_imm(1, 1);
    a.strh(1, 2, 0); // IE = VBlank
    a.strh(1, 2, 8); // IME = 1

    a.mov_imm(4, kDemoStartX);
    a.mov_imm(5, kDemoStartY);
    a.load_constant(8, kDemoSquareColor);
    a.load_constant(9, kDemoSquareAlternateColor);
    a.mov(6, 8);
    a.mov(0, 4);
    a.mov(1, 5);
    a.mov(2, 6);
    a.bl("fill_square");

    a.label("main_loop");
    a.swi(0x05); // VBlankIntrWait
    a.mov(0, 4);
    a.mov(1, 5);
    a.bl("restore_square");

    a.mov_imm(0, 0x04000000U);
    a.add_imm(0, 0, 0x100U);
    a.ldrh(1, 0, 0x30); // KEYINPUT (active low)
    a.tst_imm(1, 0x10U);
    a.add_imm(4, 4, 2, Condition::Equal); // Right
    a.tst_imm(1, 0x20U);
    a.sub_imm(4, 4, 2, Condition::Equal); // Left
    a.tst_imm(1, 0x40U);
    a.sub_imm(5, 5, 2, Condition::Equal); // Up
    a.tst_imm(1, 0x80U);
    a.add_imm(5, 5, 2, Condition::Equal); // Down
    a.tst_imm(1, 0x01U);
    a.mov(6, 9, ShiftType::LogicalLeft, 0, Condition::Equal); // A held
    a.mov(6, 8, ShiftType::LogicalLeft, 0, Condition::NotEqual);

    a.cmp_imm(4, 0);
    a.mov_imm(4, 0, Condition::LessThan);
    a.cmp_imm(4, 240 - kDemoSquareSize);
    a.mov_imm(4, 240 - kDemoSquareSize, Condition::GreaterThan);
    a.cmp_imm(5, 0);
    a.mov_imm(5, 0, Condition::LessThan);
    a.cmp_imm(5, 160 - kDemoSquareSize);
    a.mov_imm(5, 160 - kDemoSquareSize, Condition::GreaterThan);

    a.mov_imm(0, 0x02000000U);
    a.str(4, 0, 4);
    a.str(5, 0, 8);

    a.mov(0, 4);
    a.mov(1, 5);
    a.mov(2, 6);
    a.bl("fill_square");
    a.b("main_loop");
    a.literal_pool();

    // IRQ handler (ARM). The system ROM dispatcher has already saved r0-r3, r12, and lr.
    a.label("irq_handler");
    a.mov_imm(0, 0x04000000U);
    a.ldr(1, 0, 0x200);                                        // IE | IF << 16
    a.data_reg(Op::And, 1, 1, 1, ShiftType::LogicalRight, 16); // IE & IF
    a.add_imm(3, 0, 0x200U);
    a.strh(1, 3, 2);  // acknowledge in IF
    a.ldrh(2, 0, -8); // BIOS interrupt flags at 0x03007FF8
    a.data_reg(Op::Orr, 2, 2, 1);
    a.strh(2, 0, -8);
    a.mov_imm(3, kDemoFrameCounterAddress);
    a.ldr(2, 3);
    a.add_imm(2, 2, 1);
    a.str(2, 3);
    a.bx(kLr);

    // fill_square(r0 = x, r1 = y, r2 = color)
    a.label("fill_square");
    emit_pixel_address(a);
    a.mov_imm(3, kDemoSquareSize);
    a.label("fill_row");
    a.mov_imm(1, kDemoSquareSize);
    a.label("fill_column");
    a.strh_post(2, 12, 2);
    a.sub_imm(1, 1, 1, Condition::Always, true);
    a.b("fill_column", Condition::NotEqual);
    a.add_imm(12, 12, (240U - kDemoSquareSize) * 2U);
    a.sub_imm(3, 3, 1, Condition::Always, true);
    a.b("fill_row", Condition::NotEqual);
    a.bx(kLr);

    // restore_square(r0 = x, r1 = y): redraws the background under the square.
    a.label("restore_square");
    a.push({4, 5, 6, 7, kLr});
    a.mov(4, 0);
    a.mov(5, 1);
    emit_pixel_address(a);
    a.mov_imm(6, kDemoSquareSize);
    a.label("restore_row");
    emit_row_color(a);
    a.mov(2, 4);
    a.mov_imm(3, kDemoSquareSize);
    a.label("restore_column");
    emit_background_pixel(a);
    a.add_imm(2, 2, 1);
    a.sub_imm(3, 3, 1, Condition::Always, true);
    a.b("restore_column", Condition::NotEqual);
    a.add_imm(12, 12, (240U - kDemoSquareSize) * 2U);
    a.add_imm(5, 5, 1);
    a.sub_imm(6, 6, 1, Condition::Always, true);
    a.b("restore_row", Condition::NotEqual);
    a.pop({4, 5, 6, 7, kPc});

    // draw_background(): fills the whole mode 3 frame with the gradient.
    a.label("draw_background");
    a.push({4, 5, 6, 7, kLr});
    a.mov_imm(12, 0x06000000U);
    a.mov_imm(5, 0);
    a.label("background_row");
    emit_row_color(a);
    a.mov_imm(2, 0);
    a.label("background_column");
    emit_background_pixel(a);
    a.add_imm(2, 2, 1);
    a.cmp_imm(2, 240);
    a.b("background_column", Condition::NotEqual);
    a.add_imm(5, 5, 1);
    a.cmp_imm(5, 160);
    a.b("background_row", Condition::NotEqual);
    a.pop({4, 5, 6, 7, kPc});

    static_cast<void>(kSp);
    auto rom = a.finish();
    write_cartridge_header(rom, "SRGBA DEMO", "SRGD");
    // Pad to a whole kilobyte so the image looks like an ordinary cartridge dump.
    rom.resize((rom.size() + 1023U) & ~std::size_t{1023U}, 0xFFU);
    return rom;
}

} // namespace srgba::homebrew
