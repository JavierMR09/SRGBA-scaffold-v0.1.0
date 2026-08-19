#include "srgba/core/arm7tdmi.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {

using srgba::core::Arm7Tdmi;
using srgba::core::Condition;
using srgba::core::ExecutionStatus;
using srgba::core::InstructionSet;
using srgba::core::ProcessorMode;

[[nodiscard]] Arm7Tdmi thumb_cpu() noexcept {
    Arm7Tdmi cpu;
    cpu.cpsr().set_instruction_set(InstructionSet::Thumb);
    return cpu;
}

[[nodiscard]] constexpr std::uint16_t thumb_shift(const std::uint8_t operation,
                                                  const std::uint8_t amount,
                                                  const std::uint8_t source,
                                                  const std::uint8_t destination) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(operation) << 11U) |
                                      (static_cast<std::uint16_t>(amount) << 6U) |
                                      (static_cast<std::uint16_t>(source) << 3U) | destination);
}

[[nodiscard]] constexpr std::uint16_t thumb_add_subtract(const bool immediate, const bool subtract,
                                                         const std::uint8_t operand,
                                                         const std::uint8_t source,
                                                         const std::uint8_t destination) noexcept {
    return static_cast<std::uint16_t>(0x1800U | (static_cast<std::uint16_t>(immediate) << 10U) |
                                      (static_cast<std::uint16_t>(subtract) << 9U) |
                                      (static_cast<std::uint16_t>(operand) << 6U) |
                                      (static_cast<std::uint16_t>(source) << 3U) | destination);
}

[[nodiscard]] constexpr std::uint16_t thumb_immediate(const std::uint8_t operation,
                                                      const std::uint8_t destination,
                                                      const std::uint8_t immediate) noexcept {
    return static_cast<std::uint16_t>(0x2000U | (static_cast<std::uint16_t>(operation) << 11U) |
                                      (static_cast<std::uint16_t>(destination) << 8U) | immediate);
}

[[nodiscard]] constexpr std::uint16_t thumb_alu(const std::uint8_t operation,
                                                const std::uint8_t source,
                                                const std::uint8_t destination) noexcept {
    return static_cast<std::uint16_t>(0x4000U | (static_cast<std::uint16_t>(operation) << 6U) |
                                      (static_cast<std::uint16_t>(source) << 3U) | destination);
}

[[nodiscard]] constexpr std::uint16_t thumb_high(const std::uint8_t operation,
                                                 const std::uint8_t source,
                                                 const std::uint8_t destination) noexcept {
    return static_cast<std::uint16_t>(0x4400U | (static_cast<std::uint16_t>(operation) << 8U) |
                                      (static_cast<std::uint16_t>((destination >> 3U) & 1U) << 7U) |
                                      (static_cast<std::uint16_t>((source >> 3U) & 1U) << 6U) |
                                      (static_cast<std::uint16_t>(source & 7U) << 3U) |
                                      (destination & 7U));
}

struct ThumbAluVector {
    std::uint8_t opcode;
    std::uint32_t expected;
    bool writes_result;
};

} // namespace

TEST_CASE("Thumb immediate shifts implement format 1 flag behavior", "[cpu][thumb][shifter]") {
    auto cpu = thumb_cpu();
    cpu.set_register(1, 0x80000001U);

    REQUIRE(cpu.execute_thumb(thumb_shift(0, 1, 1, 0)).executed());
    REQUIRE(cpu.register_value(0) == 2U);
    REQUIRE(cpu.cpsr().carry());

    REQUIRE(cpu.execute_thumb(thumb_shift(1, 0, 1, 0)).executed());
    REQUIRE(cpu.register_value(0) == 0U);
    REQUIRE(cpu.cpsr().carry());

    REQUIRE(cpu.execute_thumb(thumb_shift(2, 0, 1, 0)).executed());
    REQUIRE(cpu.register_value(0) == 0xFFFFFFFFU);
    REQUIRE(cpu.cpsr().negative());
    REQUIRE(cpu.cpsr().carry());
}

TEST_CASE("Thumb format 2 adds and subtracts register or immediate operands", "[cpu][thumb]") {
    auto cpu = thumb_cpu();
    cpu.set_register(1, 10U);
    cpu.set_register(2, 7U);

    REQUIRE(cpu.execute_thumb(thumb_add_subtract(false, false, 2, 1, 0)).executed());
    REQUIRE(cpu.register_value(0) == 17U);

    REQUIRE(cpu.execute_thumb(thumb_add_subtract(true, true, 3, 1, 0)).executed());
    REQUIRE(cpu.register_value(0) == 7U);
    REQUIRE(cpu.cpsr().carry());
}

TEST_CASE("Thumb format 3 immediate operations update registers and flags", "[cpu][thumb]") {
    auto cpu = thumb_cpu();

    REQUIRE(cpu.execute_thumb(thumb_immediate(0, 2, 0x80)).executed()); // MOV
    REQUIRE(cpu.register_value(2) == 0x80U);

    REQUIRE(cpu.execute_thumb(thumb_immediate(2, 2, 0x80)).executed()); // ADD
    REQUIRE(cpu.register_value(2) == 0x100U);

    REQUIRE(cpu.execute_thumb(thumb_immediate(3, 2, 1)).executed()); // SUB
    REQUIRE(cpu.register_value(2) == 0xFFU);

    REQUIRE(cpu.execute_thumb(thumb_immediate(1, 2, 0xFF)).executed()); // CMP
    REQUIRE(cpu.register_value(2) == 0xFFU);
    REQUIRE(cpu.cpsr().zero());
    REQUIRE(cpu.cpsr().carry());
}

TEST_CASE("Thumb ALU instruction vectors execute all format 4 opcodes", "[cpu][thumb][vector]") {
    constexpr std::array vectors{
        ThumbAluVector{0x0, 0x00000000U, true},  // AND
        ThumbAluVector{0x1, 0x00000013U, true},  // EOR
        ThumbAluVector{0x2, 0x00000080U, true},  // LSL
        ThumbAluVector{0x3, 0x00000002U, true},  // LSR
        ThumbAluVector{0x4, 0x00000002U, true},  // ASR
        ThumbAluVector{0x5, 0x00000014U, true},  // ADC
        ThumbAluVector{0x6, 0x0000000DU, true},  // SBC
        ThumbAluVector{0x7, 0x00000002U, true},  // ROR
        ThumbAluVector{0x8, 0x00000000U, false}, // TST
        ThumbAluVector{0x9, 0xFFFFFFFDU, true},  // NEG
        ThumbAluVector{0xA, 0x0000000DU, false}, // CMP
        ThumbAluVector{0xB, 0x00000013U, false}, // CMN
        ThumbAluVector{0xC, 0x00000013U, true},  // ORR
        ThumbAluVector{0xD, 0x00000030U, true},  // MUL
        ThumbAluVector{0xE, 0x00000010U, true},  // BIC
        ThumbAluVector{0xF, 0xFFFFFFFCU, true},  // MVN
    };

    for (const auto& vector : vectors) {
        CAPTURE(vector.opcode);
        auto cpu = thumb_cpu();
        cpu.set_program_counter(0x100U);
        cpu.set_register(0, 0x10U);
        cpu.set_register(1, 0x03U);
        cpu.cpsr().set_carry(true);

        const auto result = cpu.execute_thumb(thumb_alu(vector.opcode, 1, 0));

        REQUIRE(result.executed());
        REQUIRE(cpu.program_counter() == 0x102U);
        if (vector.writes_result) {
            REQUIRE(cpu.register_value(0) == vector.expected);
        } else {
            REQUIRE(cpu.register_value(0) == 0x10U);
        }
    }
}

TEST_CASE("Thumb high-register operations access r8 through r15", "[cpu][thumb]") {
    auto cpu = thumb_cpu();
    cpu.set_register(0, 5U);

    REQUIRE(cpu.execute_thumb(thumb_high(2, 0, 8)).executed()); // MOV r8, r0
    REQUIRE(cpu.register_value(8) == 5U);

    cpu.set_register(9, 7U);
    REQUIRE(cpu.execute_thumb(thumb_high(0, 9, 8)).executed()); // ADD r8, r9
    REQUIRE(cpu.register_value(8) == 12U);

    REQUIRE(cpu.execute_thumb(thumb_high(1, 9, 8)).executed()); // CMP r8, r9
    REQUIRE_FALSE(cpu.cpsr().zero());
    REQUIRE(cpu.cpsr().carry());

    cpu.set_register(Arm7Tdmi::kLinkRegister, 0x501U);
    const auto result = cpu.execute_thumb(thumb_high(2, 14, 15)); // MOV pc, lr
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Thumb);
    REQUIRE(cpu.program_counter() == 0x500U);
}

TEST_CASE("Thumb BX selects ARM or Thumb state from target bit zero", "[cpu][thumb][branch]") {
    auto cpu = thumb_cpu();
    cpu.set_register(0, 0x600U);

    auto result = cpu.execute_thumb(thumb_high(3, 0, 0));
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Arm);
    REQUIRE(cpu.program_counter() == 0x600U);

    cpu.cpsr().set_instruction_set(InstructionSet::Thumb);
    cpu.set_program_counter(0x100U);
    cpu.set_register(0, 0x703U);
    result = cpu.execute_thumb(thumb_high(3, 0, 0));
    REQUIRE(result.executed());
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Thumb);
    REQUIRE(cpu.program_counter() == 0x702U);
}

TEST_CASE("Thumb conditional and unconditional branches use the visible PC",
          "[cpu][thumb][branch]") {
    auto cpu = thumb_cpu();
    cpu.set_program_counter(0x100U);
    cpu.cpsr().set_zero(true);

    auto result = cpu.execute_thumb(static_cast<std::uint16_t>(
        0xD000U | (static_cast<std::uint16_t>(Condition::Equal) << 8U) | 1U));
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.program_counter() == 0x106U);

    cpu.set_program_counter(0x180U);
    result = cpu.execute_thumb(static_cast<std::uint16_t>(
        0xD000U | (static_cast<std::uint16_t>(Condition::NotEqual) << 8U) | 1U));
    REQUIRE(result.status == ExecutionStatus::ConditionFailed);
    REQUIRE(cpu.program_counter() == 0x182U);

    cpu.set_program_counter(0x200U);
    result = cpu.execute_thumb(0xE7FEU); // B to the current instruction
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.program_counter() == 0x200U);
}

TEST_CASE("Thumb long branch with link executes its two-instruction sequence",
          "[cpu][thumb][branch]") {
    auto cpu = thumb_cpu();
    cpu.set_program_counter(0x100U);

    auto result = cpu.execute_thumb(0xF000U); // High offset is zero.
    REQUIRE(result.executed());
    REQUIRE_FALSE(result.pipeline_flushed);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x104U);
    REQUIRE(cpu.program_counter() == 0x102U);

    result = cpu.execute_thumb(0xF802U); // Low offset is four bytes.
    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.program_counter() == 0x108U);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x105U);
}

TEST_CASE("Thumb software interrupt saves a Thumb return address", "[cpu][thumb][exception]") {
    auto cpu = thumb_cpu();
    cpu.cpsr().set_mode(ProcessorMode::User);
    cpu.cpsr().set_irq_disabled(false);
    cpu.set_program_counter(0x300U);

    const auto result = cpu.execute_thumb(0xDF42U);

    REQUIRE(result.executed());
    REQUIRE(result.pipeline_flushed);
    REQUIRE(cpu.cpsr().mode() == ProcessorMode::Supervisor);
    REQUIRE(cpu.cpsr().instruction_set() == InstructionSet::Arm);
    REQUIRE(cpu.program_counter() == 0x08U);
    REQUIRE(cpu.register_value(Arm7Tdmi::kLinkRegister) == 0x302U);
}

TEST_CASE("The decoder rejects an instruction from the wrong state", "[cpu][thumb]") {
    Arm7Tdmi cpu;

    const auto result = cpu.execute_thumb(thumb_immediate(0, 0, 1));

    REQUIRE(result.status == ExecutionStatus::WrongInstructionSet);
    REQUIRE(cpu.program_counter() == 0U);
}
