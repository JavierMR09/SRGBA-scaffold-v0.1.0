#include "srgba/core/arm7tdmi.hpp"
#include "srgba/core/gba_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {

using srgba::core::Arm7Tdmi;
using srgba::core::Condition;
using srgba::core::GbaBus;
using srgba::core::InstructionSet;

[[nodiscard]] constexpr std::uint32_t
arm_single_transfer(const bool load, const bool byte_transfer, const bool pre_indexed,
                    const bool add_offset, const bool write_back, const std::uint8_t base,
                    const std::uint8_t data, const std::uint16_t offset,
                    const Condition condition = Condition::Always) noexcept {
    return (static_cast<std::uint32_t>(condition) << 28U) | 0x04000000U |
           (static_cast<std::uint32_t>(pre_indexed) << 24U) |
           (static_cast<std::uint32_t>(add_offset) << 23U) |
           (static_cast<std::uint32_t>(byte_transfer) << 22U) |
           (static_cast<std::uint32_t>(write_back) << 21U) |
           (static_cast<std::uint32_t>(load) << 20U) | (static_cast<std::uint32_t>(base) << 16U) |
           (static_cast<std::uint32_t>(data) << 12U) | offset;
}

[[nodiscard]] constexpr std::uint32_t
arm_halfword_transfer(const bool load, const bool signed_transfer, const bool halfword,
                      const std::uint8_t base, const std::uint8_t data,
                      const std::uint8_t offset) noexcept {
    return 0xE0000090U | (1U << 24U) | (1U << 23U) | (1U << 22U) |
           (static_cast<std::uint32_t>(load) << 20U) | (static_cast<std::uint32_t>(base) << 16U) |
           (static_cast<std::uint32_t>(data) << 12U) |
           (static_cast<std::uint32_t>(offset & 0xF0U) << 4U) |
           (static_cast<std::uint32_t>(signed_transfer) << 6U) |
           (static_cast<std::uint32_t>(halfword) << 5U) | (offset & 0x0FU);
}

[[nodiscard]] constexpr std::uint32_t arm_block_transfer(const bool load, const bool pre_indexed,
                                                         const bool increment,
                                                         const bool write_back,
                                                         const std::uint8_t base,
                                                         const std::uint16_t registers) noexcept {
    return 0xE8000000U | (static_cast<std::uint32_t>(pre_indexed) << 24U) |
           (static_cast<std::uint32_t>(increment) << 23U) |
           (static_cast<std::uint32_t>(write_back) << 21U) |
           (static_cast<std::uint32_t>(load) << 20U) | (static_cast<std::uint32_t>(base) << 16U) |
           registers;
}

} // namespace

TEST_CASE("ARM single transfers support widths indexing write-back and alignment",
          "[arm][memory]") {
    Arm7Tdmi cpu;
    GbaBus bus;
    cpu.set_program_counter(0x08000000U);
    cpu.set_register(0, 0x02000000U);
    cpu.set_register(1, 0x44332211U);

    auto result =
        cpu.execute_arm(arm_single_transfer(false, false, true, true, false, 0, 1, 4), bus);
    REQUIRE(result.executed());
    REQUIRE(result.cycles == 6U);
    REQUIRE(bus.read32(0x02000004U).value == 0x44332211U);

    cpu.set_register(0, 0x02000004U);
    result = cpu.execute_arm(arm_single_transfer(true, false, false, true, false, 0, 2, 4), bus);
    REQUIRE(result.executed());
    REQUIRE(cpu.register_value(2) == 0x44332211U);
    REQUIRE(cpu.register_value(0) == 0x02000008U);
    REQUIRE(result.cycles == 7U);

    cpu.set_register(0, 0x02000005U);
    result = cpu.execute_arm(arm_single_transfer(true, false, true, true, false, 0, 3, 0), bus);
    REQUIRE(result.executed());
    REQUIRE(cpu.register_value(3) == 0x11443322U);

    cpu.set_register(1, 0xA5U);
    result = cpu.execute_arm(arm_single_transfer(false, true, true, true, false, 0, 1, 3), bus);
    REQUIRE(result.executed());
    result = cpu.execute_arm(arm_single_transfer(true, true, true, true, false, 0, 4, 3), bus);
    REQUIRE(result.executed());
    REQUIRE(cpu.register_value(4) == 0xA5U);
}

TEST_CASE("ARM halfword and signed transfers extend loaded values correctly", "[arm][memory]") {
    Arm7Tdmi cpu;
    GbaBus bus;
    cpu.set_register(0, 0x03000000U);
    cpu.set_register(1, 0x0000BEEFU);

    REQUIRE(cpu.execute_arm(arm_halfword_transfer(false, false, true, 0, 1, 2), bus).executed());
    REQUIRE(bus.read16(0x03000002U).value == 0xBEEFU);
    REQUIRE(cpu.execute_arm(arm_halfword_transfer(true, false, true, 0, 2, 2), bus).executed());
    REQUIRE(cpu.register_value(2) == 0xBEEFU);

    static_cast<void>(bus.write8(0x03000004U, 0x80U));
    REQUIRE(cpu.execute_arm(arm_halfword_transfer(true, true, false, 0, 3, 4), bus).executed());
    REQUIRE(cpu.register_value(3) == 0xFFFFFF80U);

    static_cast<void>(bus.write16(0x03000006U, 0x8001U));
    REQUIRE(cpu.execute_arm(arm_halfword_transfer(true, true, true, 0, 4, 6), bus).executed());
    REQUIRE(cpu.register_value(4) == 0xFFFF8001U);
}

TEST_CASE("ARM block transfers preserve register order and update the base", "[arm][memory]") {
    Arm7Tdmi cpu;
    GbaBus bus;
    cpu.set_register(0, 0x03000100U);
    cpu.set_register(1, 0x11111111U);
    cpu.set_register(2, 0x22222222U);
    cpu.set_register(4, 0x44444444U);

    const auto registers = static_cast<std::uint16_t>((1U << 1U) | (1U << 2U) | (1U << 4U));
    auto result = cpu.execute_arm(arm_block_transfer(false, false, true, true, 0, registers), bus);
    REQUIRE(result.executed());
    REQUIRE(cpu.register_value(0) == 0x0300010CU);
    REQUIRE(bus.read32(0x03000100U).value == 0x11111111U);
    REQUIRE(bus.read32(0x03000104U).value == 0x22222222U);
    REQUIRE(bus.read32(0x03000108U).value == 0x44444444U);

    cpu.set_register(0, 0x03000100U);
    cpu.set_register(1, 0U);
    cpu.set_register(2, 0U);
    cpu.set_register(4, 0U);
    result = cpu.execute_arm(arm_block_transfer(true, false, true, true, 0, registers), bus);
    REQUIRE(result.executed());
    REQUIRE(cpu.register_value(1) == 0x11111111U);
    REQUIRE(cpu.register_value(2) == 0x22222222U);
    REQUIRE(cpu.register_value(4) == 0x44444444U);
    REQUIRE(cpu.register_value(0) == 0x0300010CU);
}

TEST_CASE("Thumb immediate transfers cover word byte halfword and SP-relative forms",
          "[thumb][memory]") {
    Arm7Tdmi cpu;
    GbaBus bus;
    cpu.cpsr().set_instruction_set(InstructionSet::Thumb);
    cpu.set_register(0, 0x02000000U);
    cpu.set_register(1, 0x44332211U);

    REQUIRE(cpu.execute_thumb(0x6041U, bus).executed()); // STR r1, [r0, #4]
    REQUIRE(cpu.execute_thumb(0x6842U, bus).executed()); // LDR r2, [r0, #4]
    REQUIRE(cpu.register_value(2) == 0x44332211U);

    REQUIRE(cpu.execute_thumb(0x70C1U, bus).executed()); // STRB r1, [r0, #3]
    REQUIRE(cpu.execute_thumb(0x78C3U, bus).executed()); // LDRB r3, [r0, #3]
    REQUIRE(cpu.register_value(3) == 0x11U);

    REQUIRE(cpu.execute_thumb(0x8041U, bus).executed()); // STRH r1, [r0, #2]
    REQUIRE(cpu.execute_thumb(0x8844U, bus).executed()); // LDRH r4, [r0, #2]
    REQUIRE(cpu.register_value(4) == 0x2211U);

    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03000200U);
    REQUIRE(cpu.execute_thumb(0x9101U, bus).executed()); // STR r1, [sp, #4]
    REQUIRE(cpu.execute_thumb(0x9D01U, bus).executed()); // LDR r5, [sp, #4]
    REQUIRE(cpu.register_value(5) == 0x44332211U);
}

TEST_CASE("Thumb signed loads and stack transfers use the GBA bus", "[thumb][memory]") {
    Arm7Tdmi cpu;
    GbaBus bus;
    cpu.cpsr().set_instruction_set(InstructionSet::Thumb);
    cpu.set_register(0, 0x03000000U);
    cpu.set_register(3, 4U);
    static_cast<void>(bus.write8(0x03000004U, 0x81U));

    REQUIRE(cpu.execute_thumb(0x56C2U, bus).executed()); // LDRSB r2, [r0, r3]
    REQUIRE(cpu.register_value(2) == 0xFFFFFF81U);

    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03000300U);
    cpu.set_register(0, 0x11111111U);
    cpu.set_register(1, 0x22222222U);
    cpu.set_register(Arm7Tdmi::kLinkRegister, 0x08000101U);
    REQUIRE(cpu.execute_thumb(0xB503U, bus).executed()); // PUSH {r0, r1, lr}
    REQUIRE(cpu.register_value(Arm7Tdmi::kStackPointer) == 0x030002F4U);

    cpu.set_register(0, 0U);
    cpu.set_register(1, 0U);
    const auto result = cpu.execute_thumb(0xBD03U, bus); // POP {r0, r1, pc}
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.register_value(0) == 0x11111111U);
    REQUIRE(cpu.register_value(1) == 0x22222222U);
    REQUIRE(cpu.program_counter() == 0x08000100U);
    REQUIRE(cpu.register_value(Arm7Tdmi::kStackPointer) == 0x03000300U);
}

TEST_CASE("CPU step fetches instructions and tracks Game Pak access cycles", "[cpu][bus]") {
    Arm7Tdmi cpu;
    GbaBus bus;
    const std::array<std::uint8_t, 8> code{
        0x2AU, 0x00U, 0xA0U, 0xE3U, // MOV r0, #42
        0x01U, 0x00U, 0x80U, 0xE2U, // ADD r0, r0, #1
    };
    bus.set_game_pak(code);
    cpu.set_program_counter(GbaBus::kGamePakStart);

    auto result = cpu.step(bus);
    REQUIRE(result.executed());
    REQUIRE(result.cycles == 8U);
    REQUIRE(cpu.register_value(0) == 42U);
    REQUIRE(cpu.program_counter() == GbaBus::kGamePakStart + 4U);

    result = cpu.step(bus);
    REQUIRE(result.executed());
    REQUIRE(result.cycles == 6U);
    REQUIRE(cpu.register_value(0) == 43U);
}
