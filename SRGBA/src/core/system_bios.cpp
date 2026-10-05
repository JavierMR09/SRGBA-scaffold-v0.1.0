#include "srgba/core/system_bios.hpp"

namespace srgba::core {
namespace builtin_bios {
namespace {

// ARM code. Each line lists its address and the equivalent assembly.
constexpr std::array<std::uint32_t, 40> kProgram{
    0xEAFFFFFEU, // 0x00  b     .                       ; reset (unused: SRGBA boots directly)
    0xEAFFFFFEU, // 0x04  b     .                       ; undefined instruction
    0xE1B0F00EU, // 0x08  movs  pc, lr                  ; SWI (intercepted by HleBios)
    0xEAFFFFFEU, // 0x0C  b     .                       ; prefetch abort
    0xEAFFFFFEU, // 0x10  b     .                       ; data abort
    0xEAFFFFFEU, // 0x14  b     .                       ; reserved
    // IRQ dispatcher: calls the user handler stored at 0x03007FFC (mirrored at 0x03FFFFFC).
    0xE92D500FU, // 0x18  stmfd sp!, {r0-r3, r12, lr}
    0xE3A00301U, // 0x1C  mov   r0, #0x04000000
    0xE28FE000U, // 0x20  add   lr, pc, #0
    0xE510F004U, // 0x24  ldr   pc, [r0, #-4]
    0xE8BD500FU, // 0x28  ldmfd sp!, {r0-r3, r12, lr}
    0xE25EF004U, // 0x2C  subs  pc, lr, #4
    0xEAFFFFFEU, // 0x30  b     .                       ; unused
    // Prefetched while the dispatcher returns, so it is the value left on the BIOS bus after an
    // interrupt (programs can observe it through BIOS open-bus reads).
    0xE55EC002U, // 0x34  (data)
    // IntrWait (r0 = discard old flags, r1 = interrupt mask). Entered in Supervisor mode by
    // HleBios with SPSR/LR describing the caller. Waits in System mode with IRQs enabled until
    // the game's handler sets a requested bit in the BIOS flags at 0x03007FF8.
    0xE92D500CU, // 0x38  stmfd sp!, {r2, r3, r12, lr}
    0xE14F2000U, // 0x3C  mrs   r2, spsr
    0xE92D0004U, // 0x40  stmfd sp!, {r2}
    0xE3A0C301U, // 0x44  mov   r12, #0x04000000
    0xE3A03001U, // 0x48  mov   r3, #1
    0xE5CC3208U, // 0x4C  strb  r3, [r12, #0x208]     ; IME = 1
    0xE321F01FU, // 0x50  msr   cpsr_c, #0x1F         ; System mode, IRQs enabled
    0xE3500000U, // 0x54  cmp   r0, #0
    0x0A000002U, // 0x58  beq   0x68
    0xE15C20B8U, // 0x5C  ldrh  r2, [r12, #-8]        ; discard stale requests
    0xE1C22001U, // 0x60  bic   r2, r2, r1
    0xE14C20B8U, // 0x64  strh  r2, [r12, #-8]
    0xE3A03000U, // 0x68  mov   r3, #0
    0xE5CC3301U, // 0x6C  strb  r3, [r12, #0x301]     ; HALTCNT: halt until an interrupt
    0xE321F09FU, // 0x70  msr   cpsr_c, #0x9F         ; IRQs off while checking
    0xE15C20B8U, // 0x74  ldrh  r2, [r12, #-8]
    0xE0120001U, // 0x78  ands  r0, r2, r1
    0x11C22000U, // 0x7C  bicne r2, r2, r0            ; acknowledge the bits we waited for
    0x114C20B8U, // 0x80  strneh r2, [r12, #-8]
    0x0321F01FU, // 0x84  msreq cpsr_c, #0x1F
    0x0AFFFFF6U, // 0x88  beq   0x68
    0xE321F093U, // 0x8C  msr   cpsr_c, #0x93         ; back to Supervisor mode
    0xE8BD0004U, // 0x90  ldmfd sp!, {r2}
    0xE16FF002U, // 0x94  msr   spsr_fsxc, r2
    0xE8BD500CU, // 0x98  ldmfd sp!, {r2, r3, r12, lr}
    0xE1B0F00EU, // 0x9C  movs  pc, lr
};

[[nodiscard]] std::array<std::uint8_t, kImageSize> build_image() noexcept {
    std::array<std::uint8_t, kImageSize> image{};
    for (std::size_t index = 0; index < kProgram.size(); ++index) {
        const auto word = kProgram[index];
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            image[index * 4U + byte] = static_cast<std::uint8_t>(word >> (byte * 8U));
        }
    }
    return image;
}

} // namespace

std::span<const std::uint32_t> program_words() noexcept {
    return kProgram;
}

} // namespace builtin_bios

const std::array<std::uint8_t, builtin_bios::kImageSize>& builtin_bios_image() noexcept {
    static const auto image = builtin_bios::build_image();
    return image;
}

} // namespace srgba::core
