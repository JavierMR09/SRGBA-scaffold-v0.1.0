#include "srgba/core/arm7tdmi.hpp"
#include "srgba/core/gba_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {

using srgba::core::Arm7Tdmi;
using srgba::core::GbaBus;
using srgba::core::InstructionSet;
using srgba::core::ProcessorMode;
using srgba::core::ProgramStatusRegister;

[[nodiscard]] Arm7Tdmi make_cpu() {
    Arm7Tdmi cpu;
    cpu.cpsr().set_mode(ProcessorMode::System);
    cpu.set_program_counter(0x08000000U);
    return cpu;
}

} // namespace

TEST_CASE("ARM MUL and MLA produce 32-bit products and optional flags", "[cpu][arm][multiply]") {
    auto cpu = make_cpu();
    cpu.set_register(1, 7U);
    cpu.set_register(2, 6U);
    cpu.set_register(4, 100U);

    auto result = cpu.execute_arm(0xE0000291U); // MUL r0, r1, r2
    REQUIRE(result.executed());
    REQUIRE(cpu.register_value(0) == 42U);
    REQUIRE(result.cycles == 1U);

    REQUIRE(cpu.execute_arm(0xE0234291U).executed()); // MLA r3, r1, r2, r4
    REQUIRE(cpu.register_value(3) == 142U);

    cpu.set_register(2, 0U);
    REQUIRE(cpu.execute_arm(0xE0100291U).executed()); // MULS r0, r1, r2
    REQUIRE(cpu.register_value(0) == 0U);
    REQUIRE(cpu.cpsr().zero());
    REQUIRE_FALSE(cpu.cpsr().negative());

    cpu.set_register(2, 0x00123456U);
    result = cpu.execute_arm(0xE0000291U);
    REQUIRE(result.cycles == 3U); // three significant multiplier bytes
}

TEST_CASE("ARM long multiplies handle signedness and accumulation", "[cpu][arm][multiply]") {
    auto cpu = make_cpu();
    cpu.set_register(2, 0xFFFFFFFFU);
    cpu.set_register(3, 2U);

    REQUIRE(cpu.execute_arm(0xE0810392U).executed()); // UMULL r0, r1, r2, r3
    REQUIRE(cpu.register_value(0) == 0xFFFFFFFEU);
    REQUIRE(cpu.register_value(1) == 0x00000001U);

    REQUIRE(cpu.execute_arm(0xE0C10392U).executed()); // SMULL r0, r1, r2, r3 (-1 * 2)
    REQUIRE(cpu.register_value(0) == 0xFFFFFFFEU);
    REQUIRE(cpu.register_value(1) == 0xFFFFFFFFU);

    cpu.set_register(0, 2U);
    cpu.set_register(1, 0U);
    REQUIRE(cpu.execute_arm(0xE0F10392U).executed()); // SMLALS r0, r1, r2, r3 (2 + -2)
    REQUIRE(cpu.register_value(0) == 0U);
    REQUIRE(cpu.register_value(1) == 0U);
    REQUIRE(cpu.cpsr().zero());

    cpu.set_register(0, 0xFFFFFFFFU);
    cpu.set_register(1, 0U);
    cpu.set_register(2, 1U);
    cpu.set_register(3, 1U);
    REQUIRE(cpu.execute_arm(0xE0A10392U).executed()); // UMLAL r0, r1, r2, r3
    REQUIRE(cpu.register_value(0) == 0U);
    REQUIRE(cpu.register_value(1) == 1U);
}

TEST_CASE("ARM SWP and SWPB exchange memory atomically", "[cpu][arm][memory]") {
    auto cpu = make_cpu();
    GbaBus bus;
    static_cast<void>(bus.write32(0x03000100U, 0x11223344U));
    cpu.set_register(1, 0xAABBCCDDU);
    cpu.set_register(2, 0x03000100U);

    REQUIRE(cpu.execute_arm(0xE1020091U, bus).executed()); // SWP r0, r1, [r2]
    REQUIRE(cpu.register_value(0) == 0x11223344U);
    REQUIRE(bus.read32(0x03000100U).value == 0xAABBCCDDU);

    REQUIRE(cpu.execute_arm(0xE1420091U, bus).executed()); // SWPB r0, r1, [r2]
    REQUIRE(cpu.register_value(0) == 0xDDU);
    REQUIRE(bus.read32(0x03000100U).value == 0xAABBCCDDU);
}

TEST_CASE("ARM MRS and MSR move status registers with field masks", "[cpu][arm][psr]") {
    auto cpu = make_cpu();
    cpu.cpsr().set_flags(true, false, true, false);

    REQUIRE(cpu.execute_arm(0xE10F0000U).executed()); // MRS r0, CPSR
    REQUIRE(cpu.register_value(0) == cpu.cpsr().value());

    REQUIRE(cpu.execute_arm(0xE328F4F0U).executed()); // MSR CPSR_f, #0xF0000000
    REQUIRE(cpu.cpsr().negative());
    REQUIRE(cpu.cpsr().zero());
    REQUIRE(cpu.cpsr().carry());
    REQUIRE(cpu.cpsr().overflow());
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::System);

    // Switching to IRQ mode through the control field banks SP and LR.
    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007F00U);
    cpu.set_register(1, (cpu.cpsr().value() & ~0x1FU) | 0x12U);
    REQUIRE(cpu.execute_arm(0xE121F001U).executed()); // MSR CPSR_c, r1
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::Irq);
    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007FA0U);

    cpu.set_register(2, 0x6000001FU);
    REQUIRE(cpu.execute_arm(0xE16FF002U).executed()); // MSR SPSR_fsxc, r2
    REQUIRE(cpu.spsr(ProcessorMode::Irq)->value() == 0x6000001FU);
    REQUIRE(cpu.execute_arm(0xE14F3000U).executed()); // MRS r3, SPSR
    REQUIRE(cpu.register_value(3) == 0x6000001FU);

    // MSR cannot toggle the Thumb bit.
    cpu.set_register(1, cpu.cpsr().value() | ProgramStatusRegister::kThumbMask);
    REQUIRE(cpu.execute_arm(0xE121F001U).executed());
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Arm);

    cpu.set_register(1, 0x1FU);
    REQUIRE(cpu.execute_arm(0xE121F001U).executed());
    REQUIRE(cpu.register_value(Arm7Tdmi::kStackPointer) == 0x03007F00U);
}

TEST_CASE("User mode can only change CPSR flags with MSR", "[cpu][arm][psr]") {
    auto cpu = make_cpu();
    cpu.cpsr().set_mode(ProcessorMode::User);
    cpu.set_register(1, 0xF000001FU);
    REQUIRE(cpu.execute_arm(0xE129F001U).executed()); // MSR CPSR_fc, r1
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::User);
    REQUIRE(cpu.cpsr().negative());
}

TEST_CASE("ARM block transfers support user-bank and status-restoring forms",
          "[cpu][arm][memory]") {
    auto cpu = make_cpu();
    GbaBus bus;
    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007F00U);
    cpu.set_register(Arm7Tdmi::kLinkRegister, 0x08001234U);

    cpu.cpsr().set_mode(ProcessorMode::Irq);
    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007FA0U);
    cpu.set_register(0, 0x03000200U);
    REQUIRE(cpu.execute_arm(0xE8C06000U, bus).executed()); // STMIA r0, {sp, lr}^
    REQUIRE(bus.read32(0x03000200U).value == 0x03007F00U);
    REQUIRE(bus.read32(0x03000204U).value == 0x08001234U);

    // LDMFD sp!, {r0, pc}^ returns from an exception and restores the saved status.
    cpu.spsr(ProcessorMode::Irq)->set_mode(ProcessorMode::System);
    cpu.spsr(ProcessorMode::Irq)->set_instruction_set(InstructionSet::Thumb);
    static_cast<void>(bus.write32(0x03007FA0U, 0xCAFEF00DU));
    static_cast<void>(bus.write32(0x03007FA4U, 0x08000101U));
    const auto result = cpu.execute_arm(0xE8FD8001U, bus);
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::System);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Thumb);
    REQUIRE(cpu.register_value(0) == 0xCAFEF00DU);
    REQUIRE(cpu.program_counter() == 0x08000100U);
    REQUIRE(cpu.spsr(ProcessorMode::Irq) != nullptr);
}

TEST_CASE("Block loads that include the base register skip write-back", "[cpu][memory]") {
    auto cpu = make_cpu();
    GbaBus bus;
    static_cast<void>(bus.write32(0x03000300U, 0x11111111U));
    static_cast<void>(bus.write32(0x03000304U, 0x22222222U));
    cpu.set_register(0, 0x03000300U);
    REQUIRE(cpu.execute_arm(0xE8B00003U, bus).executed()); // LDMIA r0!, {r0, r1}
    REQUIRE(cpu.register_value(0) == 0x11111111U);
    REQUIRE(cpu.register_value(1) == 0x22222222U);

    cpu.cpsr().set_instruction_set(InstructionSet::Thumb);
    cpu.set_register(2, 0x03000300U);
    REQUIRE(cpu.execute_thumb(0xCA0CU, bus).executed()); // LDMIA r2!, {r2, r3}
    REQUIRE(cpu.register_value(2) == 0x11111111U);
    REQUIRE(cpu.register_value(3) == 0x22222222U);
}
