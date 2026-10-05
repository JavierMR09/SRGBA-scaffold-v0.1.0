#include "srgba/core/arm7tdmi.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/hle_bios.hpp"
#include "srgba/core/system_bios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>

namespace {

using srgba::core::Arm7Tdmi;
using srgba::core::GbaBus;
using srgba::core::HleBios;
using srgba::core::InstructionSet;
using srgba::core::ProcessorMode;

constexpr std::uint32_t kCallSite = 0x03000000U;
constexpr std::uint32_t kData = 0x02000000U;
constexpr std::uint32_t kOutput = 0x02001000U;

class SwiHarness {
  public:
    SwiHarness() {
        cpu.cpsr().set_mode(ProcessorMode::Supervisor);
        cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007FE0U);
        cpu.cpsr().set_mode(ProcessorMode::System);
        cpu.cpsr().set_irq_disabled(false);
        cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007F00U);
    }

    void call_arm(const std::uint8_t number) {
        static_cast<void>(bus.write32(kCallSite, 0xEF000000U | (std::uint32_t{number} << 16U)));
        cpu.set_program_counter(kCallSite);
        REQUIRE(cpu.step(bus).executed());
        REQUIRE(cpu.program_counter() == srgba::core::builtin_bios::kSoftwareInterruptVector);
        REQUIRE(HleBios::pending_swi_number(cpu, bus) == number);
        static_cast<void>(HleBios::handle_swi(cpu, bus));
    }

    void write_bytes(std::uint32_t address, const std::initializer_list<std::uint8_t> bytes) {
        for (const auto value : bytes) {
            static_cast<void>(bus.write8(address++, value));
        }
    }

    Arm7Tdmi cpu;
    GbaBus bus;
};

} // namespace

TEST_CASE("HLE SWIs return to the caller with its status restored", "[bios][hle]") {
    SwiHarness harness;
    harness.cpu.cpsr().set_flags(true, false, true, false);
    harness.call_arm(0x0D); // GetBiosChecksum
    REQUIRE(harness.cpu.register_value(0) == 0xBAAE187FU);
    REQUIRE(harness.cpu.cpsr().mode() == ProcessorMode::System);
    REQUIRE(harness.cpu.cpsr().negative());
    REQUIRE(harness.cpu.cpsr().carry());
    REQUIRE_FALSE(harness.cpu.cpsr().irq_disabled());
    REQUIRE(harness.cpu.program_counter() == kCallSite + 4U);
}

TEST_CASE("Thumb SWIs read the function number from the low byte", "[bios][hle]") {
    SwiHarness harness;
    static_cast<void>(harness.bus.write16(kCallSite, 0xDF08U)); // SWI 0x08 (Sqrt)
    harness.cpu.cpsr().set_instruction_set(InstructionSet::Thumb);
    harness.cpu.set_program_counter(kCallSite);
    harness.cpu.set_register(0, 1000000U);
    REQUIRE(harness.cpu.step(harness.bus).executed());
    REQUIRE(HleBios::pending_swi_number(harness.cpu, harness.bus) == 0x08U);
    static_cast<void>(HleBios::handle_swi(harness.cpu, harness.bus));
    REQUIRE(harness.cpu.register_value(0) == 1000U);
    REQUIRE(harness.cpu.cpsr().instruction_set() == InstructionSet::Thumb);
    REQUIRE(harness.cpu.program_counter() == kCallSite + 2U);
}

TEST_CASE("Div and DivArm return quotient, remainder, and absolute quotient", "[bios][hle]") {
    SwiHarness harness;
    harness.cpu.set_register(0, static_cast<std::uint32_t>(-17));
    harness.cpu.set_register(1, 5U);
    harness.call_arm(0x06);
    REQUIRE(static_cast<std::int32_t>(harness.cpu.register_value(0)) == -3);
    REQUIRE(static_cast<std::int32_t>(harness.cpu.register_value(1)) == -2);
    REQUIRE(harness.cpu.register_value(3) == 3U);

    harness.cpu.set_register(0, 4U);
    harness.cpu.set_register(1, 100U);
    harness.call_arm(0x07);
    REQUIRE(harness.cpu.register_value(0) == 25U);
    REQUIRE(harness.cpu.register_value(1) == 0U);
}

TEST_CASE("ArcTan2 covers all four quadrants", "[bios][hle]") {
    SwiHarness harness;
    const std::array<std::array<std::int32_t, 3>, 5> cases{{
        {0x4000, 0, 0x0000},
        {0, 0x4000, 0x4000},
        {-0x4000, 0, 0x8000},
        {0, -0x4000, 0xC000},
        {0x4000, 0x4000, 0x2000},
    }};
    for (const auto& [x, y, expected] : cases) {
        harness.cpu.set_register(0, static_cast<std::uint32_t>(x));
        harness.cpu.set_register(1, static_cast<std::uint32_t>(y));
        harness.call_arm(0x0A);
        const auto angle = static_cast<std::int32_t>(harness.cpu.register_value(0));
        REQUIRE(angle >= expected - 2);
        REQUIRE(angle <= expected + 2);
    }
}

TEST_CASE("CpuSet and CpuFastSet copy and fill memory", "[bios][hle]") {
    SwiHarness harness;
    for (std::uint32_t index = 0; index < 8U; ++index) {
        static_cast<void>(harness.bus.write32(kData + index * 4U, 0x11110000U + index));
    }

    harness.cpu.set_register(0, kData);
    harness.cpu.set_register(1, kOutput);
    harness.cpu.set_register(2, 3U); // three halfwords
    harness.call_arm(0x0B);
    REQUIRE(harness.bus.read16(kOutput).value == 0x0000U);
    REQUIRE(harness.bus.read16(kOutput + 2U).value == 0x1111U);
    REQUIRE(harness.bus.read16(kOutput + 4U).value == 0x0001U);
    REQUIRE(harness.bus.read16(kOutput + 6U).value == 0x0000U);

    harness.cpu.set_register(0, kData);
    harness.cpu.set_register(1, kOutput + 0x100U);
    harness.cpu.set_register(2, (1U << 24U) | (1U << 26U) | 4U); // fill four words
    harness.call_arm(0x0B);
    REQUIRE(harness.bus.read32(kOutput + 0x10CU).value == 0x11110000U);
    REQUIRE(harness.bus.read32(kOutput + 0x110U).value == 0U);

    harness.cpu.set_register(0, kData);
    harness.cpu.set_register(1, kOutput + 0x200U);
    harness.cpu.set_register(2, 3U); // rounds up to eight words
    harness.call_arm(0x0C);
    REQUIRE(harness.bus.read32(kOutput + 0x21CU).value == 0x11110007U);

    // Copies from the BIOS region are rejected.
    harness.cpu.set_register(0, 0U);
    harness.cpu.set_register(1, kOutput + 0x300U);
    harness.cpu.set_register(2, (1U << 26U) | 1U);
    harness.call_arm(0x0B);
    REQUIRE(harness.bus.read32(kOutput + 0x300U).value == 0U);
}

TEST_CASE("LZ77 and run-length decompression write WRAM and VRAM", "[bios][hle]") {
    SwiHarness harness;
    // "ABABABAB": two literals then a 6-byte back-reference with displacement 2.
    harness.write_bytes(kData, {0x10, 0x08, 0x00, 0x00, 0x20, 'A', 'B', 0x30, 0x01});
    harness.cpu.set_register(0, kData);
    harness.cpu.set_register(1, kOutput);
    harness.call_arm(0x11);
    REQUIRE(harness.bus.read32(kOutput).value == 0x42414241U);
    REQUIRE(harness.bus.read32(kOutput + 4U).value == 0x42414241U);

    harness.cpu.set_register(0, kData);
    harness.cpu.set_register(1, 0x06000000U);
    harness.call_arm(0x12);
    REQUIRE(harness.bus.read32(0x06000004U).value == 0x42414241U);

    // "AAAAAB": a run of five 'A' then one literal 'B'.
    harness.write_bytes(kData + 0x40U, {0x30, 0x06, 0x00, 0x00, 0x82, 'A', 0x00, 'B'});
    harness.cpu.set_register(0, kData + 0x40U);
    harness.cpu.set_register(1, kOutput + 0x40U);
    harness.call_arm(0x14);
    REQUIRE(harness.bus.read32(kOutput + 0x40U).value == 0x41414141U);
    REQUIRE(harness.bus.read16(kOutput + 0x44U).value == 0x4241U);
}

TEST_CASE("Huffman, BitUnPack, and diff filters decode data", "[bios][hle]") {
    SwiHarness harness;
    // Two-leaf tree: bit 0 -> 'X', bit 1 -> 'Y'. Stream 0110 decodes to "XYYX".
    harness.write_bytes(kData, {0x28, 0x04, 0x00, 0x00, 0x01, 0xC0, 'X', 'Y'});
    static_cast<void>(harness.bus.write32(kData + 8U, 0x60000000U));
    harness.cpu.set_register(0, kData);
    harness.cpu.set_register(1, kOutput);
    harness.call_arm(0x13);
    REQUIRE(harness.bus.read32(kOutput).value == 0x58595958U);

    harness.write_bytes(kData + 0x40U, {0xE4});
    harness.write_bytes(kData + 0x50U, {0x01, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00});
    harness.cpu.set_register(0, kData + 0x40U);
    harness.cpu.set_register(1, kOutput + 0x40U);
    harness.cpu.set_register(2, kData + 0x50U);
    harness.call_arm(0x10);
    REQUIRE(harness.bus.read32(kOutput + 0x40U).value == 0x03020100U);

    harness.write_bytes(kData + 0x80U, {0x81, 0x04, 0x00, 0x00, 1, 1, 1, 1});
    harness.cpu.set_register(0, kData + 0x80U);
    harness.cpu.set_register(1, kOutput + 0x80U);
    harness.call_arm(0x16);
    REQUIRE(harness.bus.read32(kOutput + 0x80U).value == 0x04030201U);
}

TEST_CASE("IntrWait continues in the built-in wait routine", "[bios][hle][irq]") {
    SwiHarness harness;
    harness.cpu.set_register(0, 7U);
    harness.cpu.set_register(1, 9U);
    harness.call_arm(0x05);
    REQUIRE(harness.cpu.program_counter() == srgba::core::builtin_bios::kIntrWaitRoutine);
    REQUIRE(harness.cpu.cpsr().mode() == ProcessorMode::Supervisor);
    REQUIRE(harness.cpu.register_value(0) == 1U);
    REQUIRE(harness.cpu.register_value(1) == 1U);
}
