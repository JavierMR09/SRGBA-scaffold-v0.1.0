#include "srgba/core/arm7tdmi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {

using srgba::core::Arm7Tdmi;
using srgba::core::Condition;
using srgba::core::ExceptionType;
using srgba::core::InstructionSet;
using srgba::core::ProcessorMode;
using srgba::core::ProgramStatusRegister;
using srgba::core::ShiftType;

} // namespace

TEST_CASE("ARM7TDMI reset enters masked ARM supervisor mode", "[cpu][state]") {
    const Arm7Tdmi cpu;

    REQUIRE(cpu.cpsr().mode() == ProcessorMode::Supervisor);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Arm);
    REQUIRE(cpu.cpsr().irq_disabled());
    REQUIRE(cpu.cpsr().fiq_disabled());
    REQUIRE(cpu.program_counter() == 0U);
    for (std::size_t index = 0; index < Arm7Tdmi::kRegisterCount; ++index) {
        REQUIRE(cpu.register_value(index) == 0U);
    }
}

TEST_CASE("ARM7TDMI register banks survive processor mode changes", "[cpu][state][banking]") {
    Arm7Tdmi cpu;
    cpu.cpsr().set_mode(ProcessorMode::User);
    cpu.set_register(8, 0x108U);
    cpu.set_register(13, 0x10DU);
    cpu.set_register(14, 0x10EU);

    cpu.cpsr().set_mode(ProcessorMode::Fiq);
    REQUIRE(cpu.register_value(8) == 0U);
    REQUIRE(cpu.register_value(13) == 0U);
    cpu.set_register(8, 0xF08U);
    cpu.set_register(13, 0xF0DU);
    cpu.set_register(14, 0xF0EU);

    cpu.cpsr().set_mode(ProcessorMode::Irq);
    REQUIRE(cpu.register_value(8) == 0x108U);
    REQUIRE(cpu.register_value(13) == 0U);
    cpu.set_register(13, 0x120DU);

    cpu.cpsr().set_mode(ProcessorMode::System);
    REQUIRE(cpu.register_value(8) == 0x108U);
    REQUIRE(cpu.register_value(13) == 0x10DU);
    REQUIRE(cpu.register_value(14) == 0x10EU);

    cpu.cpsr().set_mode(ProcessorMode::Fiq);
    REQUIRE(cpu.register_value(8) == 0xF08U);
    REQUIRE(cpu.register_value(13) == 0xF0DU);
    REQUIRE(cpu.register_value(14) == 0xF0EU);

    cpu.cpsr().set_mode(ProcessorMode::Irq);
    REQUIRE(cpu.register_value(13) == 0x120DU);
}

TEST_CASE("Program status registers preserve flags and reject invalid modes", "[cpu][psr]") {
    ProgramStatusRegister status_register;
    constexpr std::uint32_t value = 0xA00000D3U;

    REQUIRE(status_register.assign(value));
    REQUIRE(status_register.value() == value);
    REQUIRE(status_register.negative());
    REQUIRE_FALSE(status_register.zero());
    REQUIRE(status_register.carry());
    REQUIRE_FALSE(status_register.overflow());
    REQUIRE(status_register.irq_disabled());
    REQUIRE(status_register.fiq_disabled());
    REQUIRE(status_register.mode() == ProcessorMode::Supervisor);

    REQUIRE_FALSE(status_register.assign(0U));
    REQUIRE(status_register.value() == value);
}

TEST_CASE("Every ARM condition is evaluated from CPSR flags", "[cpu][condition]") {
    Arm7Tdmi cpu;
    cpu.cpsr().set_flags(true, false, true, false);

    constexpr std::array expected{
        false, true,  true,  false, true,  false, false, true,
        true,  false, false, true,  false, true,  true,  false,
    };

    for (std::size_t value = 0; value < expected.size(); ++value) {
        INFO("condition value: " << value);
        REQUIRE(cpu.condition_passed(static_cast<Condition>(value)) == expected[value]);
    }
}

TEST_CASE("The barrel shifter implements ARM zero and boundary cases", "[cpu][shifter]") {
    auto result = srgba::core::shift_by_immediate(ShiftType::LogicalLeft, 0x80000001U, 0, true);
    REQUIRE(result.value == 0x80000001U);
    REQUIRE(result.carry);

    result = srgba::core::shift_by_immediate(ShiftType::LogicalRight, 0x80000001U, 0, false);
    REQUIRE(result.value == 0U);
    REQUIRE(result.carry);

    result = srgba::core::shift_by_immediate(ShiftType::ArithmeticRight, 0x80000001U, 0, false);
    REQUIRE(result.value == 0xFFFFFFFFU);
    REQUIRE(result.carry);

    result = srgba::core::shift_by_immediate(ShiftType::RotateRight, 0x00000003U, 0, true);
    REQUIRE(result.value == 0x80000001U);
    REQUIRE(result.carry);

    result = srgba::core::shift_by_register(ShiftType::LogicalLeft, 0x00000001U, 32, false);
    REQUIRE(result.value == 0U);
    REQUIRE(result.carry);

    result = srgba::core::shift_by_register(ShiftType::LogicalLeft, 0xFFFFFFFFU, 33, true);
    REQUIRE(result.value == 0U);
    REQUIRE_FALSE(result.carry);

    result = srgba::core::shift_by_register(ShiftType::RotateRight, 0x80000001U, 32, false);
    REQUIRE(result.value == 0x80000001U);
    REQUIRE(result.carry);
}

TEST_CASE("Software interrupts bank state and MOVS PC LR returns from the exception",
          "[cpu][exception]") {
    Arm7Tdmi cpu;
    cpu.cpsr().set_mode(ProcessorMode::User);
    cpu.cpsr().set_instruction_set(InstructionSet::Thumb);
    cpu.cpsr().set_irq_disabled(false);
    cpu.cpsr().set_fiq_disabled(false);
    cpu.set_program_counter(0x100U);

    cpu.take_exception(ExceptionType::SoftwareInterrupt);

    REQUIRE(cpu.cpsr().mode() == ProcessorMode::Supervisor);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Arm);
    REQUIRE(cpu.cpsr().irq_disabled());
    REQUIRE_FALSE(cpu.cpsr().fiq_disabled());
    REQUIRE(cpu.program_counter() == 0x08U);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x102U);

    const auto* saved = cpu.current_spsr();
    REQUIRE(saved != nullptr);
    REQUIRE(saved->mode() == ProcessorMode::User);
    REQUIRE(saved->instruction_set() == InstructionSet::Thumb);

    // MOVS PC, LR restores CPSR from SPSR_svc as part of exception return.
    const auto result = cpu.execute_arm(0xE1B0F00EU);
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::User);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Thumb);
    REQUIRE(cpu.program_counter() == 0x102U);
}

TEST_CASE("IRQ and FIQ requests respect their CPSR masks", "[cpu][exception]") {
    Arm7Tdmi cpu;
    REQUIRE_FALSE(cpu.try_take_irq());
    REQUIRE_FALSE(cpu.try_take_fiq());

    cpu.cpsr().set_mode(ProcessorMode::User);
    cpu.cpsr().set_irq_disabled(false);
    cpu.cpsr().set_fiq_disabled(false);
    cpu.set_program_counter(0x200U);

    REQUIRE(cpu.try_take_irq());
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::Irq);
    REQUIRE(cpu.program_counter() == 0x18U);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x204U);

    cpu.reset();
    cpu.cpsr().set_mode(ProcessorMode::User);
    cpu.cpsr().set_fiq_disabled(false);
    cpu.set_program_counter(0x300U);

    REQUIRE(cpu.try_take_fiq());
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::Fiq);
    REQUIRE(cpu.cpsr().irq_disabled());
    REQUIRE(cpu.cpsr().fiq_disabled());
    REQUIRE(cpu.program_counter() == 0x1CU);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x304U);
}
