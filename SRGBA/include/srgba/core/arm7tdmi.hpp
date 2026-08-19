#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace srgba::core {

enum class ProcessorMode : std::uint8_t {
    User = 0x10,
    Fiq = 0x11,
    Irq = 0x12,
    Supervisor = 0x13,
    Abort = 0x17,
    Undefined = 0x1B,
    System = 0x1F,
};

enum class InstructionSet : std::uint8_t {
    Arm,
    Thumb,
};

enum class Condition : std::uint8_t {
    Equal = 0x0,
    NotEqual = 0x1,
    CarrySet = 0x2,
    CarryClear = 0x3,
    Minus = 0x4,
    Plus = 0x5,
    Overflow = 0x6,
    NoOverflow = 0x7,
    Higher = 0x8,
    LowerOrSame = 0x9,
    GreaterOrEqual = 0xA,
    LessThan = 0xB,
    GreaterThan = 0xC,
    LessOrEqual = 0xD,
    Always = 0xE,
    Never = 0xF,
};

enum class ShiftType : std::uint8_t {
    LogicalLeft,
    LogicalRight,
    ArithmeticRight,
    RotateRight,
};

enum class ExceptionType : std::uint8_t {
    Reset,
    UndefinedInstruction,
    SoftwareInterrupt,
    PrefetchAbort,
    DataAbort,
    Irq,
    Fiq,
};

enum class ExecutionStatus : std::uint8_t {
    Executed,
    ConditionFailed,
    UnsupportedInstruction,
    WrongInstructionSet,
};

struct ShiftResult {
    std::uint32_t value{};
    bool carry{};
};

struct ExecutionResult {
    ExecutionStatus status{ExecutionStatus::Executed};
    bool pipeline_flushed{};

    [[nodiscard]] bool executed() const noexcept {
        return status == ExecutionStatus::Executed;
    }
};

[[nodiscard]] bool is_valid_processor_mode(std::uint32_t value) noexcept;

[[nodiscard]] ShiftResult shift_by_immediate(ShiftType type, std::uint32_t value,
                                             std::uint8_t amount, bool carry_in) noexcept;
[[nodiscard]] ShiftResult shift_by_register(ShiftType type, std::uint32_t value,
                                            std::uint8_t amount, bool carry_in) noexcept;

class ProgramStatusRegister {
  public:
    static constexpr std::uint32_t kNegativeMask = 1U << 31U;
    static constexpr std::uint32_t kZeroMask = 1U << 30U;
    static constexpr std::uint32_t kCarryMask = 1U << 29U;
    static constexpr std::uint32_t kOverflowMask = 1U << 28U;
    static constexpr std::uint32_t kIrqDisableMask = 1U << 7U;
    static constexpr std::uint32_t kFiqDisableMask = 1U << 6U;
    static constexpr std::uint32_t kThumbMask = 1U << 5U;
    static constexpr std::uint32_t kModeMask = 0x1FU;

    ProgramStatusRegister() = default;

    [[nodiscard]] std::uint32_t value() const noexcept;
    [[nodiscard]] bool assign(std::uint32_t value) noexcept;

    [[nodiscard]] bool negative() const noexcept;
    [[nodiscard]] bool zero() const noexcept;
    [[nodiscard]] bool carry() const noexcept;
    [[nodiscard]] bool overflow() const noexcept;
    [[nodiscard]] bool irq_disabled() const noexcept;
    [[nodiscard]] bool fiq_disabled() const noexcept;
    [[nodiscard]] InstructionSet instruction_set() const noexcept;
    [[nodiscard]] ProcessorMode mode() const noexcept;

    void set_negative(bool set) noexcept;
    void set_zero(bool set) noexcept;
    void set_carry(bool set) noexcept;
    void set_overflow(bool set) noexcept;
    void set_irq_disabled(bool disabled) noexcept;
    void set_fiq_disabled(bool disabled) noexcept;
    void set_instruction_set(InstructionSet instruction_set) noexcept;
    void set_mode(ProcessorMode mode) noexcept;
    void set_flags(bool negative, bool zero, bool carry, bool overflow) noexcept;

  private:
    void set_bit(std::uint32_t mask, bool set) noexcept;

    std::uint32_t value_{static_cast<std::uint32_t>(ProcessorMode::User)};
};

class Arm7Tdmi {
  public:
    static constexpr std::size_t kRegisterCount = 16;
    static constexpr std::size_t kStackPointer = 13;
    static constexpr std::size_t kLinkRegister = 14;
    static constexpr std::size_t kProgramCounter = 15;

    Arm7Tdmi() noexcept;

    void reset() noexcept;

    // r15 stores the address of the instruction being fetched. Operand reads expose the
    // architecture's pipelined PC value (+8 in ARM state and +4 in Thumb state).
    [[nodiscard]] std::uint32_t register_value(std::size_t index) const noexcept;
    void set_register(std::size_t index, std::uint32_t value) noexcept;
    [[nodiscard]] std::uint32_t program_counter() const noexcept;
    void set_program_counter(std::uint32_t value) noexcept;

    [[nodiscard]] ProgramStatusRegister& cpsr() noexcept;
    [[nodiscard]] const ProgramStatusRegister& cpsr() const noexcept;
    [[nodiscard]] ProgramStatusRegister* spsr(ProcessorMode mode) noexcept;
    [[nodiscard]] const ProgramStatusRegister* spsr(ProcessorMode mode) const noexcept;
    [[nodiscard]] ProgramStatusRegister* current_spsr() noexcept;
    [[nodiscard]] const ProgramStatusRegister* current_spsr() const noexcept;

    [[nodiscard]] bool condition_passed(Condition condition) const noexcept;
    [[nodiscard]] ExecutionResult execute_arm(std::uint32_t instruction) noexcept;
    [[nodiscard]] ExecutionResult execute_thumb(std::uint16_t instruction) noexcept;

    void take_exception(ExceptionType exception) noexcept;
    [[nodiscard]] bool try_take_irq() noexcept;
    [[nodiscard]] bool try_take_fiq() noexcept;

  private:
    struct ArithmeticResult {
        std::uint32_t value{};
        bool carry{};
        bool overflow{};
    };

    [[nodiscard]] std::uint32_t arm_operand_register(std::size_t index,
                                                     bool register_shift) const noexcept;
    [[nodiscard]] std::uint32_t thumb_operand_register(std::size_t index) const noexcept;
    void branch_to(std::uint32_t target) noexcept;
    void advance_arm() noexcept;
    void advance_thumb() noexcept;
    void set_nz(std::uint32_t value) noexcept;
    void set_logical_flags(std::uint32_t value, bool carry) noexcept;
    void set_arithmetic_flags(const ArithmeticResult& result) noexcept;

    [[nodiscard]] static ArithmeticResult add(std::uint32_t left, std::uint32_t right,
                                              bool carry_in) noexcept;
    [[nodiscard]] static ArithmeticResult subtract(std::uint32_t left, std::uint32_t right,
                                                   bool borrow) noexcept;

    std::array<std::uint32_t, 8> low_registers_{};
    std::array<std::uint32_t, 5> user_high_registers_{};
    std::array<std::uint32_t, 5> fiq_high_registers_{};
    std::array<std::uint32_t, 2> user_sp_lr_{};
    std::array<std::uint32_t, 2> fiq_sp_lr_{};
    std::array<std::uint32_t, 2> supervisor_sp_lr_{};
    std::array<std::uint32_t, 2> abort_sp_lr_{};
    std::array<std::uint32_t, 2> irq_sp_lr_{};
    std::array<std::uint32_t, 2> undefined_sp_lr_{};
    std::uint32_t program_counter_{};

    ProgramStatusRegister cpsr_{};
    ProgramStatusRegister spsr_fiq_{};
    ProgramStatusRegister spsr_supervisor_{};
    ProgramStatusRegister spsr_abort_{};
    ProgramStatusRegister spsr_irq_{};
    ProgramStatusRegister spsr_undefined_{};
};

} // namespace srgba::core
