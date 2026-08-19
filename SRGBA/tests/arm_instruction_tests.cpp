#include "srgba/core/arm7tdmi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {

using srgba::core::Arm7Tdmi;
using srgba::core::Condition;
using srgba::core::ExecutionStatus;
using srgba::core::InstructionSet;
using srgba::core::ShiftType;

[[nodiscard]] constexpr std::uint32_t
arm_data_register(const std::uint8_t opcode, const bool set_flags, const std::uint8_t first,
                  const std::uint8_t destination, const std::uint8_t second,
                  const Condition condition = Condition::Always,
                  const ShiftType shift = ShiftType::LogicalLeft,
                  const std::uint8_t amount = 0) noexcept {
    return (static_cast<std::uint32_t>(condition) << 28U) |
           (static_cast<std::uint32_t>(opcode) << 21U) |
           (static_cast<std::uint32_t>(set_flags) << 20U) |
           (static_cast<std::uint32_t>(first) << 16U) |
           (static_cast<std::uint32_t>(destination) << 12U) |
           (static_cast<std::uint32_t>(amount) << 7U) | (static_cast<std::uint32_t>(shift) << 5U) |
           second;
}

[[nodiscard]] constexpr std::uint32_t
arm_data_register_shift(const std::uint8_t opcode, const bool set_flags, const std::uint8_t first,
                        const std::uint8_t destination, const std::uint8_t second,
                        const std::uint8_t shift_register,
                        const ShiftType shift = ShiftType::LogicalLeft) noexcept {
    return (static_cast<std::uint32_t>(Condition::Always) << 28U) |
           (static_cast<std::uint32_t>(opcode) << 21U) |
           (static_cast<std::uint32_t>(set_flags) << 20U) |
           (static_cast<std::uint32_t>(first) << 16U) |
           (static_cast<std::uint32_t>(destination) << 12U) |
           (static_cast<std::uint32_t>(shift_register) << 8U) |
           (static_cast<std::uint32_t>(shift) << 5U) | (1U << 4U) | second;
}

[[nodiscard]] constexpr std::uint32_t
arm_data_immediate(const std::uint8_t opcode, const bool set_flags, const std::uint8_t first,
                   const std::uint8_t destination, const std::uint8_t immediate,
                   const std::uint8_t rotate = 0,
                   const Condition condition = Condition::Always) noexcept {
    return (static_cast<std::uint32_t>(condition) << 28U) | (1U << 25U) |
           (static_cast<std::uint32_t>(opcode) << 21U) |
           (static_cast<std::uint32_t>(set_flags) << 20U) |
           (static_cast<std::uint32_t>(first) << 16U) |
           (static_cast<std::uint32_t>(destination) << 12U) |
           (static_cast<std::uint32_t>(rotate) << 8U) | immediate;
}

struct DataProcessingVector {
    std::uint8_t opcode;
    std::uint32_t expected;
    bool writes_result;
};

} // namespace

TEST_CASE("ARM data-processing instruction vectors execute all base opcodes",
          "[cpu][arm][vector]") {
    constexpr std::array vectors{
        DataProcessingVector{0x0, 0x00000000U, true},  // AND
        DataProcessingVector{0x1, 0x00000013U, true},  // EOR
        DataProcessingVector{0x2, 0x0000000DU, true},  // SUB
        DataProcessingVector{0x3, 0xFFFFFFF3U, true},  // RSB
        DataProcessingVector{0x4, 0x00000013U, true},  // ADD
        DataProcessingVector{0x5, 0x00000014U, true},  // ADC
        DataProcessingVector{0x6, 0x0000000DU, true},  // SBC
        DataProcessingVector{0x7, 0xFFFFFFF3U, true},  // RSC
        DataProcessingVector{0x8, 0x00000000U, false}, // TST
        DataProcessingVector{0x9, 0x00000013U, false}, // TEQ
        DataProcessingVector{0xA, 0x0000000DU, false}, // CMP
        DataProcessingVector{0xB, 0x00000013U, false}, // CMN
        DataProcessingVector{0xC, 0x00000013U, true},  // ORR
        DataProcessingVector{0xD, 0x00000003U, true},  // MOV
        DataProcessingVector{0xE, 0x00000010U, true},  // BIC
        DataProcessingVector{0xF, 0xFFFFFFFCU, true},  // MVN
    };

    for (const auto& vector : vectors) {
        CAPTURE(vector.opcode);
        Arm7Tdmi cpu;
        cpu.set_program_counter(0x100U);
        cpu.set_register(0, 0xAAAAAAAAU);
        cpu.set_register(1, 0x10U);
        cpu.set_register(2, 0x03U);
        cpu.cpsr().set_carry(true);

        const auto result =
            cpu.execute_arm(arm_data_register(vector.opcode, true, 1, 0, 2, Condition::Always));

        REQUIRE(result.executed());
        REQUIRE_FALSE(result.pipeline_flushed);
        REQUIRE(cpu.program_counter() == 0x104U);
        if (vector.writes_result) {
            REQUIRE(cpu.register_value(0) == vector.expected);
        } else {
            REQUIRE(cpu.register_value(0) == 0xAAAAAAAAU);
        }
    }
}

TEST_CASE("ARM arithmetic instructions produce carry and signed overflow flags", "[cpu][arm]") {
    Arm7Tdmi cpu;
    cpu.set_register(1, 0x7FFFFFFFU);
    cpu.set_register(2, 1U);

    REQUIRE(cpu.execute_arm(arm_data_register(0x4, true, 1, 0, 2)).executed());
    REQUIRE(cpu.register_value(0) == 0x80000000U);
    REQUIRE(cpu.cpsr().negative());
    REQUIRE_FALSE(cpu.cpsr().zero());
    REQUIRE_FALSE(cpu.cpsr().carry());
    REQUIRE(cpu.cpsr().overflow());

    cpu.set_register(1, 0xFFFFFFFFU);
    REQUIRE(cpu.execute_arm(arm_data_register(0x4, true, 1, 0, 2)).executed());
    REQUIRE(cpu.register_value(0) == 0U);
    REQUIRE(cpu.cpsr().zero());
    REQUIRE(cpu.cpsr().carry());
    REQUIRE_FALSE(cpu.cpsr().overflow());

    cpu.set_register(1, 0x80000000U);
    REQUIRE(cpu.execute_arm(arm_data_register(0x2, true, 1, 0, 2)).executed());
    REQUIRE(cpu.register_value(0) == 0x7FFFFFFFU);
    REQUIRE(cpu.cpsr().carry());
    REQUIRE(cpu.cpsr().overflow());
}

TEST_CASE("ARM immediate rotation feeds the ALU and shifter carry", "[cpu][arm][shifter]") {
    Arm7Tdmi cpu;
    cpu.cpsr().set_overflow(true);

    const auto result = cpu.execute_arm(arm_data_immediate(0xD, true, 0, 0, 0xFF, 4));

    REQUIRE(result.executed());
    REQUIRE(cpu.register_value(0) == 0xFF000000U);
    REQUIRE(cpu.cpsr().negative());
    REQUIRE(cpu.cpsr().carry());
    REQUIRE(cpu.cpsr().overflow());
}

TEST_CASE("ARM PC operands expose the correct pipeline offset", "[cpu][arm][pipeline]") {
    Arm7Tdmi cpu;
    cpu.set_program_counter(0x100U);

    REQUIRE(cpu.execute_arm(arm_data_immediate(0x4, false, 15, 0, 4)).executed());
    REQUIRE(cpu.register_value(0) == 0x10CU);

    cpu.set_program_counter(0x200U);
    cpu.set_register(1, 1U);
    cpu.set_register(2, 1U);
    REQUIRE(cpu.execute_arm(arm_data_register_shift(0x4, false, 15, 0, 1, 2)).executed());
    REQUIRE(cpu.register_value(0) == 0x20EU);
}

TEST_CASE("ARM condition failure consumes an instruction without changing its destination",
          "[cpu][arm][condition]") {
    Arm7Tdmi cpu;
    cpu.set_program_counter(0x100U);
    cpu.set_register(0, 7U);
    cpu.cpsr().set_zero(true);

    const auto result =
        cpu.execute_arm(arm_data_immediate(0xD, false, 0, 0, 1, 0, Condition::NotEqual));

    REQUIRE(result.status == ExecutionStatus::ConditionFailed);
    REQUIRE(cpu.register_value(0) == 7U);
    REQUIRE(cpu.program_counter() == 0x104U);
}

TEST_CASE("ARM branch, link, and exchange operations flush the pipeline", "[cpu][arm][branch]") {
    Arm7Tdmi cpu;
    cpu.set_program_counter(0x100U);

    auto result = cpu.execute_arm(0xEA000000U); // B +0
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.program_counter() == 0x108U);

    cpu.set_program_counter(0x200U);
    result = cpu.execute_arm(0xEB000000U); // BL +0
    REQUIRE(result.executed());
    REQUIRE(cpu.program_counter() == 0x208U);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x204U);

    cpu.set_program_counter(0x280U);
    cpu.set_register(0, 0x301U);
    result = cpu.execute_arm(0xE12FFF10U); // BX r0
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Thumb);
    REQUIRE(cpu.program_counter() == 0x300U);
}

TEST_CASE("ARM software interrupt enters the supervisor vector", "[cpu][arm][exception]") {
    Arm7Tdmi cpu;
    cpu.cpsr().set_mode(srgba::core::ProcessorMode::User);
    cpu.cpsr().set_irq_disabled(false);
    cpu.set_program_counter(0x400U);

    const auto result = cpu.execute_arm(0xEF000123U);

    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.cpsr().mode() == srgba::core::ProcessorMode::Supervisor);
    REQUIRE(cpu.program_counter() == 0x08U);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x404U);
}

TEST_CASE("Unsupported ARM encodings leave CPU state available for a later decoder", "[cpu][arm]") {
    Arm7Tdmi cpu;
    cpu.set_program_counter(0x100U);

    const auto result = cpu.execute_arm(0xE0000291U); // MUL encoding, outside M1 base ARM ALU

    REQUIRE(result.status == ExecutionStatus::UnsupportedInstruction);
    REQUIRE(cpu.program_counter() == 0x100U);
}
