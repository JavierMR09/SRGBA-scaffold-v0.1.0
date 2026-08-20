#include "srgba/core/arm7tdmi.hpp"

#include "srgba/core/gba_bus.hpp"

#include <bit>
#include <cassert>
#include <cstdint>
#include <limits>

namespace srgba::core {
namespace {

[[nodiscard]] constexpr bool bit(const std::uint32_t value, const unsigned index) noexcept {
    return ((value >> index) & 1U) != 0U;
}

[[nodiscard]] constexpr std::int32_t sign_extend(const std::uint32_t value,
                                                 const unsigned width) noexcept {
    const auto sign = 1U << (width - 1U);
    return static_cast<std::int32_t>((value ^ sign) - sign);
}

[[nodiscard]] constexpr std::uint32_t add_signed(const std::uint32_t base,
                                                 const std::int32_t offset) noexcept {
    return base + static_cast<std::uint32_t>(offset);
}

[[nodiscard]] constexpr std::uint32_t arithmetic_shift_right(const std::uint32_t value,
                                                             const unsigned amount) noexcept {
    const auto shifted = value >> amount;
    return bit(value, 31) ? shifted | (~0U << (32U - amount)) : shifted;
}

[[nodiscard]] constexpr ExecutionResult executed(const bool pipeline_flushed = false,
                                                 const std::uint32_t cycles = 0) noexcept {
    return {ExecutionStatus::Executed, pipeline_flushed, cycles};
}

[[nodiscard]] constexpr ExecutionResult status(const ExecutionStatus value) noexcept {
    return {value, false};
}

} // namespace

bool is_valid_processor_mode(const std::uint32_t value) noexcept {
    switch (value & ProgramStatusRegister::kModeMask) {
    case static_cast<std::uint32_t>(ProcessorMode::User):
    case static_cast<std::uint32_t>(ProcessorMode::Fiq):
    case static_cast<std::uint32_t>(ProcessorMode::Irq):
    case static_cast<std::uint32_t>(ProcessorMode::Supervisor):
    case static_cast<std::uint32_t>(ProcessorMode::Abort):
    case static_cast<std::uint32_t>(ProcessorMode::Undefined):
    case static_cast<std::uint32_t>(ProcessorMode::System):
        return true;
    default:
        return false;
    }
}

ShiftResult shift_by_immediate(const ShiftType type, const std::uint32_t value,
                               const std::uint8_t amount, const bool carry_in) noexcept {
    switch (type) {
    case ShiftType::LogicalLeft:
        if (amount == 0U) {
            return {value, carry_in};
        }
        if (amount < 32U) {
            return {value << amount, bit(value, 32U - amount)};
        }
        if (amount == 32U) {
            return {0, bit(value, 0)};
        }
        return {0, false};

    case ShiftType::LogicalRight:
        if (amount == 0U || amount == 32U) {
            return {0, bit(value, 31)};
        }
        if (amount < 32U) {
            return {value >> amount, bit(value, amount - 1U)};
        }
        return {0, false};

    case ShiftType::ArithmeticRight:
        if (amount == 0U || amount >= 32U) {
            const auto fill = bit(value, 31) ? std::numeric_limits<std::uint32_t>::max() : 0U;
            return {fill, bit(value, 31)};
        }
        return {arithmetic_shift_right(value, amount), bit(value, amount - 1U)};

    case ShiftType::RotateRight:
        if (amount == 0U) {
            return {(static_cast<std::uint32_t>(carry_in) << 31U) | (value >> 1U), bit(value, 0)};
        }
        {
            const auto rotation = static_cast<unsigned>(amount) & 31U;
            if (rotation == 0U) {
                return {value, bit(value, 31)};
            }
            const auto result = std::rotr(value, static_cast<int>(rotation));
            return {result, bit(result, 31)};
        }
    }

    return {value, carry_in};
}

ShiftResult shift_by_register(const ShiftType type, const std::uint32_t value,
                              const std::uint8_t amount, const bool carry_in) noexcept {
    if (amount == 0U) {
        return {value, carry_in};
    }

    switch (type) {
    case ShiftType::LogicalLeft:
        if (amount < 32U) {
            return {value << amount, bit(value, 32U - amount)};
        }
        if (amount == 32U) {
            return {0, bit(value, 0)};
        }
        return {0, false};

    case ShiftType::LogicalRight:
        if (amount < 32U) {
            return {value >> amount, bit(value, amount - 1U)};
        }
        if (amount == 32U) {
            return {0, bit(value, 31)};
        }
        return {0, false};

    case ShiftType::ArithmeticRight:
        if (amount < 32U) {
            return {arithmetic_shift_right(value, amount), bit(value, amount - 1U)};
        }
        {
            const auto fill = bit(value, 31) ? std::numeric_limits<std::uint32_t>::max() : 0U;
            return {fill, bit(value, 31)};
        }

    case ShiftType::RotateRight: {
        const auto rotation = static_cast<unsigned>(amount) & 31U;
        if (rotation == 0U) {
            return {value, bit(value, 31)};
        }
        const auto result = std::rotr(value, static_cast<int>(rotation));
        return {result, bit(result, 31)};
    }
    }

    return {value, carry_in};
}

std::uint32_t ProgramStatusRegister::value() const noexcept {
    return value_;
}

bool ProgramStatusRegister::assign(const std::uint32_t value) noexcept {
    if (!is_valid_processor_mode(value)) {
        return false;
    }
    value_ = value;
    return true;
}

bool ProgramStatusRegister::negative() const noexcept {
    return (value_ & kNegativeMask) != 0U;
}

bool ProgramStatusRegister::zero() const noexcept {
    return (value_ & kZeroMask) != 0U;
}

bool ProgramStatusRegister::carry() const noexcept {
    return (value_ & kCarryMask) != 0U;
}

bool ProgramStatusRegister::overflow() const noexcept {
    return (value_ & kOverflowMask) != 0U;
}

bool ProgramStatusRegister::irq_disabled() const noexcept {
    return (value_ & kIrqDisableMask) != 0U;
}

bool ProgramStatusRegister::fiq_disabled() const noexcept {
    return (value_ & kFiqDisableMask) != 0U;
}

InstructionSet ProgramStatusRegister::instruction_set() const noexcept {
    return (value_ & kThumbMask) != 0U ? InstructionSet::Thumb : InstructionSet::Arm;
}

ProcessorMode ProgramStatusRegister::mode() const noexcept {
    return static_cast<ProcessorMode>(value_ & kModeMask);
}

void ProgramStatusRegister::set_negative(const bool set) noexcept {
    set_bit(kNegativeMask, set);
}

void ProgramStatusRegister::set_zero(const bool set) noexcept {
    set_bit(kZeroMask, set);
}

void ProgramStatusRegister::set_carry(const bool set) noexcept {
    set_bit(kCarryMask, set);
}

void ProgramStatusRegister::set_overflow(const bool set) noexcept {
    set_bit(kOverflowMask, set);
}

void ProgramStatusRegister::set_irq_disabled(const bool disabled) noexcept {
    set_bit(kIrqDisableMask, disabled);
}

void ProgramStatusRegister::set_fiq_disabled(const bool disabled) noexcept {
    set_bit(kFiqDisableMask, disabled);
}

void ProgramStatusRegister::set_instruction_set(const InstructionSet instruction_set) noexcept {
    set_bit(kThumbMask, instruction_set == InstructionSet::Thumb);
}

void ProgramStatusRegister::set_mode(const ProcessorMode mode) noexcept {
    value_ = (value_ & ~kModeMask) | static_cast<std::uint32_t>(mode);
}

void ProgramStatusRegister::set_flags(const bool negative, const bool zero, const bool carry,
                                      const bool overflow) noexcept {
    set_negative(negative);
    set_zero(zero);
    set_carry(carry);
    set_overflow(overflow);
}

void ProgramStatusRegister::set_bit(const std::uint32_t mask, const bool set) noexcept {
    if (set) {
        value_ |= mask;
    } else {
        value_ &= ~mask;
    }
}

Arm7Tdmi::Arm7Tdmi() noexcept {
    reset();
}

void Arm7Tdmi::reset() noexcept {
    low_registers_.fill(0);
    user_high_registers_.fill(0);
    fiq_high_registers_.fill(0);
    user_sp_lr_.fill(0);
    fiq_sp_lr_.fill(0);
    supervisor_sp_lr_.fill(0);
    abort_sp_lr_.fill(0);
    irq_sp_lr_.fill(0);
    undefined_sp_lr_.fill(0);
    program_counter_ = 0;
    next_fetch_sequential_ = false;

    cpsr_ = ProgramStatusRegister{};
    cpsr_.set_mode(ProcessorMode::Supervisor);
    cpsr_.set_instruction_set(InstructionSet::Arm);
    cpsr_.set_irq_disabled(true);
    cpsr_.set_fiq_disabled(true);

    spsr_fiq_ = ProgramStatusRegister{};
    spsr_supervisor_ = ProgramStatusRegister{};
    spsr_abort_ = ProgramStatusRegister{};
    spsr_irq_ = ProgramStatusRegister{};
    spsr_undefined_ = ProgramStatusRegister{};
}

std::uint32_t Arm7Tdmi::register_value(const std::size_t index) const noexcept {
    assert(index < kRegisterCount);
    if (index < 8U) {
        return low_registers_[index];
    }
    if (index < 13U) {
        const auto& bank =
            cpsr_.mode() == ProcessorMode::Fiq ? fiq_high_registers_ : user_high_registers_;
        return bank[index - 8U];
    }
    if (index < 15U) {
        const auto bank_index = index - 13U;
        switch (cpsr_.mode()) {
        case ProcessorMode::Fiq:
            return fiq_sp_lr_[bank_index];
        case ProcessorMode::Irq:
            return irq_sp_lr_[bank_index];
        case ProcessorMode::Supervisor:
            return supervisor_sp_lr_[bank_index];
        case ProcessorMode::Abort:
            return abort_sp_lr_[bank_index];
        case ProcessorMode::Undefined:
            return undefined_sp_lr_[bank_index];
        case ProcessorMode::User:
        case ProcessorMode::System:
            return user_sp_lr_[bank_index];
        }
    }
    return program_counter_;
}

void Arm7Tdmi::set_register(const std::size_t index, const std::uint32_t value) noexcept {
    assert(index < kRegisterCount);
    if (index < 8U) {
        low_registers_[index] = value;
        return;
    }
    if (index < 13U) {
        auto& bank =
            cpsr_.mode() == ProcessorMode::Fiq ? fiq_high_registers_ : user_high_registers_;
        bank[index - 8U] = value;
        return;
    }
    if (index < 15U) {
        const auto bank_index = index - 13U;
        switch (cpsr_.mode()) {
        case ProcessorMode::Fiq:
            fiq_sp_lr_[bank_index] = value;
            return;
        case ProcessorMode::Irq:
            irq_sp_lr_[bank_index] = value;
            return;
        case ProcessorMode::Supervisor:
            supervisor_sp_lr_[bank_index] = value;
            return;
        case ProcessorMode::Abort:
            abort_sp_lr_[bank_index] = value;
            return;
        case ProcessorMode::Undefined:
            undefined_sp_lr_[bank_index] = value;
            return;
        case ProcessorMode::User:
        case ProcessorMode::System:
            user_sp_lr_[bank_index] = value;
            return;
        }
    }
    program_counter_ = value;
}

std::uint32_t Arm7Tdmi::program_counter() const noexcept {
    return program_counter_;
}

void Arm7Tdmi::set_program_counter(const std::uint32_t value) noexcept {
    branch_to(value);
}

ProgramStatusRegister& Arm7Tdmi::cpsr() noexcept {
    return cpsr_;
}

const ProgramStatusRegister& Arm7Tdmi::cpsr() const noexcept {
    return cpsr_;
}

ProgramStatusRegister* Arm7Tdmi::spsr(const ProcessorMode mode) noexcept {
    switch (mode) {
    case ProcessorMode::Fiq:
        return &spsr_fiq_;
    case ProcessorMode::Irq:
        return &spsr_irq_;
    case ProcessorMode::Supervisor:
        return &spsr_supervisor_;
    case ProcessorMode::Abort:
        return &spsr_abort_;
    case ProcessorMode::Undefined:
        return &spsr_undefined_;
    case ProcessorMode::User:
    case ProcessorMode::System:
        return nullptr;
    }
    return nullptr;
}

const ProgramStatusRegister* Arm7Tdmi::spsr(const ProcessorMode mode) const noexcept {
    return const_cast<Arm7Tdmi*>(this)->spsr(mode);
}

ProgramStatusRegister* Arm7Tdmi::current_spsr() noexcept {
    return spsr(cpsr_.mode());
}

const ProgramStatusRegister* Arm7Tdmi::current_spsr() const noexcept {
    return spsr(cpsr_.mode());
}

bool Arm7Tdmi::condition_passed(const Condition condition) const noexcept {
    const auto negative = cpsr_.negative();
    const auto zero = cpsr_.zero();
    const auto carry = cpsr_.carry();
    const auto overflow = cpsr_.overflow();

    switch (condition) {
    case Condition::Equal:
        return zero;
    case Condition::NotEqual:
        return !zero;
    case Condition::CarrySet:
        return carry;
    case Condition::CarryClear:
        return !carry;
    case Condition::Minus:
        return negative;
    case Condition::Plus:
        return !negative;
    case Condition::Overflow:
        return overflow;
    case Condition::NoOverflow:
        return !overflow;
    case Condition::Higher:
        return carry && !zero;
    case Condition::LowerOrSame:
        return !carry || zero;
    case Condition::GreaterOrEqual:
        return negative == overflow;
    case Condition::LessThan:
        return negative != overflow;
    case Condition::GreaterThan:
        return !zero && negative == overflow;
    case Condition::LessOrEqual:
        return zero || negative != overflow;
    case Condition::Always:
        return true;
    case Condition::Never:
        return false;
    }
    return false;
}

Arm7Tdmi::ArithmeticResult Arm7Tdmi::add(const std::uint32_t left, const std::uint32_t right,
                                         const bool carry_in) noexcept {
    const auto unsigned_result =
        static_cast<std::uint64_t>(left) + right + static_cast<std::uint64_t>(carry_in);
    const auto signed_result = static_cast<std::int64_t>(static_cast<std::int32_t>(left)) +
                               static_cast<std::int64_t>(static_cast<std::int32_t>(right)) +
                               static_cast<std::int64_t>(carry_in);
    return {
        static_cast<std::uint32_t>(unsigned_result),
        (unsigned_result >> 32U) != 0U,
        signed_result > std::numeric_limits<std::int32_t>::max() ||
            signed_result < std::numeric_limits<std::int32_t>::min(),
    };
}

Arm7Tdmi::ArithmeticResult Arm7Tdmi::subtract(const std::uint32_t left, const std::uint32_t right,
                                              const bool borrow) noexcept {
    const auto subtrahend = static_cast<std::uint64_t>(right) + static_cast<std::uint64_t>(borrow);
    const auto signed_result = static_cast<std::int64_t>(static_cast<std::int32_t>(left)) -
                               static_cast<std::int64_t>(static_cast<std::int32_t>(right)) -
                               static_cast<std::int64_t>(borrow);
    return {
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(left) - subtrahend),
        static_cast<std::uint64_t>(left) >= subtrahend,
        signed_result > std::numeric_limits<std::int32_t>::max() ||
            signed_result < std::numeric_limits<std::int32_t>::min(),
    };
}

std::uint32_t Arm7Tdmi::arm_operand_register(const std::size_t index,
                                             const bool register_shift) const noexcept {
    if (index == kProgramCounter) {
        return program_counter_ + (register_shift ? 12U : 8U);
    }
    return register_value(index);
}

std::uint32_t Arm7Tdmi::thumb_operand_register(const std::size_t index) const noexcept {
    if (index == kProgramCounter) {
        return (program_counter_ + 4U) & ~1U;
    }
    return register_value(index);
}

void Arm7Tdmi::branch_to(const std::uint32_t target) noexcept {
    if (cpsr_.instruction_set() == InstructionSet::Thumb) {
        program_counter_ = target & ~1U;
    } else {
        program_counter_ = target & ~3U;
    }
    next_fetch_sequential_ = false;
}

void Arm7Tdmi::advance_arm() noexcept {
    program_counter_ += 4U;
}

void Arm7Tdmi::advance_thumb() noexcept {
    program_counter_ += 2U;
}

void Arm7Tdmi::set_nz(const std::uint32_t value) noexcept {
    cpsr_.set_negative(bit(value, 31));
    cpsr_.set_zero(value == 0U);
}

void Arm7Tdmi::set_logical_flags(const std::uint32_t value, const bool carry) noexcept {
    set_nz(value);
    cpsr_.set_carry(carry);
}

void Arm7Tdmi::set_arithmetic_flags(const ArithmeticResult& result) noexcept {
    set_nz(result.value);
    cpsr_.set_carry(result.carry);
    cpsr_.set_overflow(result.overflow);
}

ExecutionResult Arm7Tdmi::execute_arm_single_transfer(const std::uint32_t instruction,
                                                      GbaBus& bus) noexcept {
    const bool register_offset = bit(instruction, 25);
    const bool pre_indexed = bit(instruction, 24);
    const bool add_offset = bit(instruction, 23);
    const bool byte_transfer = bit(instruction, 22);
    const bool write_back = bit(instruction, 21) || !pre_indexed;
    const bool load = bit(instruction, 20);
    const auto base_register = static_cast<std::size_t>((instruction >> 16U) & 0xFU);
    const auto data_register = static_cast<std::size_t>((instruction >> 12U) & 0xFU);

    if (write_back && base_register == kProgramCounter) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }
    if (load && byte_transfer && data_register == kProgramCounter) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }
    if (load && write_back && data_register == base_register) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    std::uint32_t offset = instruction & 0xFFFU;
    if (register_offset) {
        if (bit(instruction, 4)) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const auto type = static_cast<ShiftType>((instruction >> 5U) & 0x3U);
        const auto amount = static_cast<std::uint8_t>((instruction >> 7U) & 0x1FU);
        const auto source = static_cast<std::size_t>(instruction & 0xFU);
        offset =
            shift_by_immediate(type, arm_operand_register(source, false), amount, cpsr_.carry())
                .value;
    }

    const auto base = arm_operand_register(base_register, false);
    const auto adjusted = add_offset ? base + offset : base - offset;
    const auto address = pre_indexed ? adjusted : base;
    const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, program_counter_};

    if (load) {
        const auto read = byte_transfer ? bus.read8(address, access) : bus.read32(address, access);
        if (write_back) {
            set_register(base_register, adjusted);
        }
        if (data_register == kProgramCounter) {
            branch_to(read.value);
            return executed(true, read.cycles + 1U);
        }
        set_register(data_register, read.value);
        advance_arm();
        return executed(false, read.cycles + 1U);
    }

    const auto value =
        data_register == kProgramCounter ? program_counter_ + 12U : register_value(data_register);
    const auto write = byte_transfer ? bus.write8(address, static_cast<std::uint8_t>(value), access)
                                     : bus.write32(address, value, access);
    if (write_back) {
        set_register(base_register, adjusted);
    }
    advance_arm();
    return executed(false, write.cycles);
}

ExecutionResult Arm7Tdmi::execute_arm_halfword_transfer(const std::uint32_t instruction,
                                                        GbaBus& bus) noexcept {
    const bool pre_indexed = bit(instruction, 24);
    const bool add_offset = bit(instruction, 23);
    const bool immediate_offset = bit(instruction, 22);
    const bool write_back = bit(instruction, 21) || !pre_indexed;
    const bool load = bit(instruction, 20);
    const bool signed_transfer = bit(instruction, 6);
    const bool halfword = bit(instruction, 5);
    const auto base_register = static_cast<std::size_t>((instruction >> 16U) & 0xFU);
    const auto data_register = static_cast<std::size_t>((instruction >> 12U) & 0xFU);

    if ((!load && signed_transfer) || (!signed_transfer && !halfword) ||
        (load && data_register == kProgramCounter) ||
        (write_back && base_register == kProgramCounter) ||
        (load && write_back && data_register == base_register)) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    std::uint32_t offset = 0;
    if (immediate_offset) {
        offset = ((instruction >> 8U) & 0xFU) << 4U;
        offset |= instruction & 0xFU;
    } else {
        if ((instruction & 0x00000F00U) != 0U || (instruction & 0xFU) == kProgramCounter) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        offset = register_value(instruction & 0xFU);
    }

    const auto base = arm_operand_register(base_register, false);
    const auto adjusted = add_offset ? base + offset : base - offset;
    const auto address = pre_indexed ? adjusted : base;
    const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, program_counter_};

    if (!load) {
        const auto source = data_register == kProgramCounter ? program_counter_ + 12U
                                                             : register_value(data_register);
        const auto value = static_cast<std::uint16_t>(source);
        const auto write = bus.write16(address, value, access);
        if (write_back) {
            set_register(base_register, adjusted);
        }
        advance_arm();
        return executed(false, write.cycles);
    }

    std::uint32_t value = 0;
    std::uint32_t cycles = 0;
    if (signed_transfer && !halfword) {
        const auto read = bus.read8(address, access);
        value = static_cast<std::uint32_t>(
            static_cast<std::int32_t>(static_cast<std::int8_t>(read.value)));
        cycles = read.cycles;
    } else {
        const auto read = bus.read16(address, access);
        value = signed_transfer ? static_cast<std::uint32_t>(static_cast<std::int32_t>(
                                      static_cast<std::int16_t>(read.value)))
                                : read.value;
        cycles = read.cycles;
    }

    if (write_back) {
        set_register(base_register, adjusted);
    }
    set_register(data_register, value);
    advance_arm();
    return executed(false, cycles + 1U);
}

ExecutionResult Arm7Tdmi::execute_arm_block_transfer(const std::uint32_t instruction,
                                                     GbaBus& bus) noexcept {
    const bool pre_indexed = bit(instruction, 24);
    const bool increment = bit(instruction, 23);
    const bool load_psr_or_user_bank = bit(instruction, 22);
    const bool write_back = bit(instruction, 21);
    const bool load = bit(instruction, 20);
    const auto base_register = static_cast<std::size_t>((instruction >> 16U) & 0xFU);
    const auto register_list = static_cast<std::uint16_t>(instruction);
    const auto register_count = static_cast<std::uint32_t>(std::popcount(register_list));

    if (register_list == 0U || load_psr_or_user_bank || base_register == kProgramCounter ||
        (load && write_back && bit(register_list, static_cast<unsigned>(base_register)))) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    const auto base = register_value(base_register);
    std::uint32_t address = 0;
    if (increment) {
        address = pre_indexed ? base + 4U : base;
    } else {
        address = pre_indexed ? base - register_count * 4U : base - (register_count - 1U) * 4U;
    }

    std::uint32_t cycles = 0;
    bool first_access = true;
    bool loaded_program_counter = false;
    std::uint32_t loaded_pc_value = 0;
    for (std::size_t register_index = 0; register_index < kRegisterCount; ++register_index) {
        if (!bit(register_list, static_cast<unsigned>(register_index))) {
            continue;
        }
        const BusAccess access{
            first_access ? AccessSequence::NonSequential : AccessSequence::Sequential,
            AccessKind::Data,
            program_counter_,
        };
        first_access = false;

        if (load) {
            const auto read = bus.read32(address, access);
            cycles += read.cycles;
            if (register_index == kProgramCounter) {
                loaded_program_counter = true;
                loaded_pc_value = read.value;
            } else {
                set_register(register_index, read.value);
            }
        } else {
            const auto value = register_index == kProgramCounter ? program_counter_ + 12U
                                                                 : register_value(register_index);
            cycles += bus.write32(address, value, access).cycles;
        }
        address += 4U;
    }

    if (write_back) {
        const auto updated_base =
            increment ? base + register_count * 4U : base - register_count * 4U;
        set_register(base_register, updated_base);
    }
    if (loaded_program_counter) {
        branch_to(loaded_pc_value);
        return executed(true, cycles + 1U);
    }
    advance_arm();
    return executed(false, cycles + static_cast<std::uint32_t>(load));
}

ExecutionResult Arm7Tdmi::execute_arm(const std::uint32_t instruction) noexcept {
    return execute_arm_impl(instruction, nullptr);
}

ExecutionResult Arm7Tdmi::execute_arm(const std::uint32_t instruction, GbaBus& bus) noexcept {
    return execute_arm_impl(instruction, &bus);
}

ExecutionResult Arm7Tdmi::execute_thumb(const std::uint16_t instruction) noexcept {
    return execute_thumb_impl(instruction, nullptr);
}

ExecutionResult Arm7Tdmi::execute_thumb(const std::uint16_t instruction, GbaBus& bus) noexcept {
    return execute_thumb_impl(instruction, &bus);
}

ExecutionResult Arm7Tdmi::step(GbaBus& bus) noexcept {
    const BusAccess fetch_access{
        next_fetch_sequential_ ? AccessSequence::Sequential : AccessSequence::NonSequential,
        AccessKind::Instruction,
        program_counter_,
    };

    ExecutionResult result;
    std::uint32_t fetch_cycles = 0;
    if (cpsr_.instruction_set() == InstructionSet::Arm) {
        const auto fetch = bus.read32(program_counter_, fetch_access);
        fetch_cycles = fetch.cycles;
        result = execute_arm_impl(fetch.value, &bus);
    } else {
        const auto fetch = bus.read16(program_counter_, fetch_access);
        fetch_cycles = fetch.cycles;
        result = execute_thumb_impl(static_cast<std::uint16_t>(fetch.value), &bus);
    }

    result.cycles += fetch_cycles;
    const bool completed = result.status == ExecutionStatus::Executed ||
                           result.status == ExecutionStatus::ConditionFailed;
    next_fetch_sequential_ = completed && !result.pipeline_flushed;
    return result;
}

ExecutionResult Arm7Tdmi::execute_arm_impl(const std::uint32_t instruction,
                                           GbaBus* const bus) noexcept {
    if (cpsr_.instruction_set() != InstructionSet::Arm) {
        return status(ExecutionStatus::WrongInstructionSet);
    }

    const auto condition = static_cast<Condition>((instruction >> 28U) & 0xFU);
    if (condition == Condition::Never) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }
    if (!condition_passed(condition)) {
        advance_arm();
        return status(ExecutionStatus::ConditionFailed);
    }

    // Branch and exchange must be decoded before the generic data-processing class.
    if ((instruction & 0x0FFFFFF0U) == 0x012FFF10U) {
        const auto target = arm_operand_register(instruction & 0xFU, false);
        cpsr_.set_instruction_set(bit(target, 0) ? InstructionSet::Thumb : InstructionSet::Arm);
        branch_to(target);
        return executed(true);
    }

    if ((instruction & 0x0F000000U) == 0x0F000000U) {
        take_exception(ExceptionType::SoftwareInterrupt);
        return executed(true);
    }

    if ((instruction & 0x0E000000U) == 0x0A000000U) {
        const auto link = bit(instruction, 24);
        const auto offset = sign_extend((instruction & 0x00FFFFFFU) << 2U, 26U);
        if (link) {
            set_register(kLinkRegister, program_counter_ + 4U);
        }
        branch_to(add_signed(program_counter_ + 8U, offset));
        return executed(true);
    }

    if ((instruction & 0x0E000000U) == 0x08000000U) {
        return bus ? execute_arm_block_transfer(instruction, *bus)
                   : status(ExecutionStatus::UnsupportedInstruction);
    }

    if ((instruction & 0x0C000000U) == 0x04000000U) {
        return bus ? execute_arm_single_transfer(instruction, *bus)
                   : status(ExecutionStatus::UnsupportedInstruction);
    }

    if ((instruction & 0x0C000000U) != 0U) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    // Multiply and halfword-transfer encodings overlap the data-processing class.
    if ((instruction & 0x0E000090U) == 0x00000090U) {
        const bool halfword_or_signed_transfer = (instruction & 0x60U) != 0U;
        if (halfword_or_signed_transfer && bus) {
            return execute_arm_halfword_transfer(instruction, *bus);
        }
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    const auto opcode = static_cast<std::uint8_t>((instruction >> 21U) & 0xFU);
    const auto set_flags = bit(instruction, 20);
    const auto test_operation = opcode >= 8U && opcode <= 11U;
    if (test_operation && !set_flags) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    const auto destination = static_cast<std::size_t>((instruction >> 12U) & 0xFU);
    const auto writes_result = !test_operation;
    if (writes_result && destination == kProgramCounter && set_flags && current_spsr() == nullptr) {
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    ShiftResult operand2{};
    bool register_shift = false;
    if (bit(instruction, 25)) {
        const auto immediate = instruction & 0xFFU;
        const auto rotation = static_cast<unsigned>((instruction >> 8U) & 0xFU) * 2U;
        operand2.value = std::rotr(immediate, static_cast<int>(rotation));
        operand2.carry = rotation == 0U ? cpsr_.carry() : bit(operand2.value, 31);
    } else {
        const auto type = static_cast<ShiftType>((instruction >> 5U) & 0x3U);
        const auto source = static_cast<std::size_t>(instruction & 0xFU);
        register_shift = bit(instruction, 4);
        if (register_shift) {
            if (bit(instruction, 7)) {
                return status(ExecutionStatus::UnsupportedInstruction);
            }
            const auto shift_register = static_cast<std::size_t>((instruction >> 8U) & 0xFU);
            if (shift_register == kProgramCounter) {
                return status(ExecutionStatus::UnsupportedInstruction);
            }
            const auto amount = static_cast<std::uint8_t>(register_value(shift_register) & 0xFFU);
            operand2 =
                shift_by_register(type, arm_operand_register(source, true), amount, cpsr_.carry());
        } else {
            const auto amount = static_cast<std::uint8_t>((instruction >> 7U) & 0x1FU);
            operand2 = shift_by_immediate(type, arm_operand_register(source, false), amount,
                                          cpsr_.carry());
        }
    }

    const auto first_operand =
        arm_operand_register(static_cast<std::size_t>((instruction >> 16U) & 0xFU), register_shift);

    std::uint32_t result = 0;
    ArithmeticResult arithmetic{};
    bool arithmetic_operation = false;

    switch (opcode) {
    case 0x0: // AND
    case 0x8: // TST
        result = first_operand & operand2.value;
        break;
    case 0x1: // EOR
    case 0x9: // TEQ
        result = first_operand ^ operand2.value;
        break;
    case 0x2: // SUB
    case 0xA: // CMP
        arithmetic = subtract(first_operand, operand2.value, false);
        arithmetic_operation = true;
        result = arithmetic.value;
        break;
    case 0x3: // RSB
        arithmetic = subtract(operand2.value, first_operand, false);
        arithmetic_operation = true;
        result = arithmetic.value;
        break;
    case 0x4: // ADD
    case 0xB: // CMN
        arithmetic = add(first_operand, operand2.value, false);
        arithmetic_operation = true;
        result = arithmetic.value;
        break;
    case 0x5: // ADC
        arithmetic = add(first_operand, operand2.value, cpsr_.carry());
        arithmetic_operation = true;
        result = arithmetic.value;
        break;
    case 0x6: // SBC
        arithmetic = subtract(first_operand, operand2.value, !cpsr_.carry());
        arithmetic_operation = true;
        result = arithmetic.value;
        break;
    case 0x7: // RSC
        arithmetic = subtract(operand2.value, first_operand, !cpsr_.carry());
        arithmetic_operation = true;
        result = arithmetic.value;
        break;
    case 0xC: // ORR
        result = first_operand | operand2.value;
        break;
    case 0xD: // MOV
        result = operand2.value;
        break;
    case 0xE: // BIC
        result = first_operand & ~operand2.value;
        break;
    case 0xF: // MVN
        result = ~operand2.value;
        break;
    default:
        return status(ExecutionStatus::UnsupportedInstruction);
    }

    if (!writes_result) {
        if (arithmetic_operation) {
            set_arithmetic_flags(arithmetic);
        } else {
            set_logical_flags(result, operand2.carry);
        }
        advance_arm();
        return executed();
    }

    if (destination == kProgramCounter) {
        if (set_flags) {
            const auto saved_status = *current_spsr();
            const auto assigned = cpsr_.assign(saved_status.value());
            assert(assigned);
            static_cast<void>(assigned);
        }
        branch_to(result);
        return executed(true);
    }

    set_register(destination, result);
    if (set_flags) {
        if (arithmetic_operation) {
            set_arithmetic_flags(arithmetic);
        } else {
            set_logical_flags(result, operand2.carry);
        }
    }
    advance_arm();
    return executed();
}

ExecutionResult Arm7Tdmi::execute_thumb_impl(const std::uint16_t instruction,
                                             GbaBus* const bus) noexcept {
    if (cpsr_.instruction_set() != InstructionSet::Thumb) {
        return status(ExecutionStatus::WrongInstructionSet);
    }

    // Format 19: first and second halves of long branch with link.
    if ((instruction & 0xF800U) == 0xF000U) {
        const auto high_offset = sign_extend(instruction & 0x07FFU, 11U) * 4096;
        set_register(kLinkRegister, add_signed(program_counter_ + 4U, high_offset));
        advance_thumb();
        return executed();
    }
    if ((instruction & 0xF800U) == 0xF800U) {
        const auto target = register_value(kLinkRegister) +
                            (static_cast<std::uint32_t>(instruction & 0x07FFU) << 1U);
        set_register(kLinkRegister, (program_counter_ + 2U) | 1U);
        branch_to(target);
        return executed(true);
    }

    // Format 18: unconditional branch.
    if ((instruction & 0xF800U) == 0xE000U) {
        const auto offset = sign_extend((instruction & 0x07FFU) << 1U, 12U);
        branch_to(add_signed(program_counter_ + 4U, offset));
        return executed(true);
    }

    // Formats 16 and 17: conditional branch and software interrupt.
    if ((instruction & 0xF000U) == 0xD000U) {
        const auto condition = static_cast<Condition>((instruction >> 8U) & 0xFU);
        if (condition == Condition::Never) {
            take_exception(ExceptionType::SoftwareInterrupt);
            return executed(true);
        }
        if (condition == Condition::Always) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        if (!condition_passed(condition)) {
            advance_thumb();
            return status(ExecutionStatus::ConditionFailed);
        }
        const auto offset = sign_extend((instruction & 0x00FFU) << 1U, 9U);
        branch_to(add_signed(program_counter_ + 4U, offset));
        return executed(true);
    }

    // Format 5: high-register operations and BX.
    if ((instruction & 0xFC00U) == 0x4400U) {
        const auto operation = static_cast<std::uint8_t>((instruction >> 8U) & 0x3U);
        const auto high_destination = (instruction & 0x0080U) != 0U;
        const auto high_source = (instruction & 0x0040U) != 0U;
        const auto source =
            static_cast<std::size_t>(((instruction >> 3U) & 0x7U) | (high_source ? 0x8U : 0U));
        const auto destination =
            static_cast<std::size_t>((instruction & 0x7U) | (high_destination ? 0x8U : 0U));

        if (operation != 0x3U && !high_destination && !high_source) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }

        if (operation == 0x3U) {
            if (high_destination || (instruction & 0x7U) != 0U) {
                return status(ExecutionStatus::UnsupportedInstruction);
            }
            const auto target = thumb_operand_register(source);
            cpsr_.set_instruction_set(bit(target, 0) ? InstructionSet::Thumb : InstructionSet::Arm);
            branch_to(target);
            return executed(true);
        }

        const auto left = thumb_operand_register(destination);
        const auto right = thumb_operand_register(source);
        if (operation == 0x0U) {
            const auto value = left + right;
            if (destination == kProgramCounter) {
                branch_to(value);
                return executed(true);
            }
            set_register(destination, value);
        } else if (operation == 0x1U) {
            set_arithmetic_flags(subtract(left, right, false));
        } else {
            if (destination == kProgramCounter) {
                branch_to(right);
                return executed(true);
            }
            set_register(destination, right);
        }
        advance_thumb();
        return executed();
    }

    // Format 6: PC-relative word load.
    if ((instruction & 0xF800U) == 0x4800U) {
        if (!bus) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const auto destination = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const auto address = ((program_counter_ + 4U) & ~3U) +
                             (static_cast<std::uint32_t>(instruction & 0xFFU) << 2U);
        const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, program_counter_};
        const auto read = bus->read32(address, access);
        set_register(destination, read.value);
        advance_thumb();
        return executed(false, read.cycles + 1U);
    }

    // Formats 7 and 8: register-offset transfers, including signed loads.
    if ((instruction & 0xF000U) == 0x5000U) {
        if (!bus) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const auto offset_register = static_cast<std::size_t>((instruction >> 6U) & 0x7U);
        const auto base_register = static_cast<std::size_t>((instruction >> 3U) & 0x7U);
        const auto data_register = static_cast<std::size_t>(instruction & 0x7U);
        const auto address = register_value(base_register) + register_value(offset_register);
        const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, program_counter_};

        if (!bit(instruction, 9)) {
            const bool load = bit(instruction, 11);
            const bool byte_transfer = bit(instruction, 10);
            if (load) {
                const auto read =
                    byte_transfer ? bus->read8(address, access) : bus->read32(address, access);
                set_register(data_register, read.value);
                advance_thumb();
                return executed(false, read.cycles + 1U);
            }
            const auto value = register_value(data_register);
            const auto write = byte_transfer
                                   ? bus->write8(address, static_cast<std::uint8_t>(value), access)
                                   : bus->write32(address, value, access);
            advance_thumb();
            return executed(false, write.cycles);
        }

        const bool halfword = bit(instruction, 11);
        const bool signed_transfer = bit(instruction, 10);
        if (!signed_transfer && !halfword) {
            const auto write = bus->write16(
                address, static_cast<std::uint16_t>(register_value(data_register)), access);
            advance_thumb();
            return executed(false, write.cycles);
        }

        std::uint32_t value = 0;
        std::uint32_t cycles = 0;
        if (signed_transfer && !halfword) {
            const auto read = bus->read8(address, access);
            value = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int8_t>(read.value)));
            cycles = read.cycles;
        } else {
            const auto read = bus->read16(address, access);
            value = signed_transfer ? static_cast<std::uint32_t>(static_cast<std::int32_t>(
                                          static_cast<std::int16_t>(read.value)))
                                    : read.value;
            cycles = read.cycles;
        }
        set_register(data_register, value);
        advance_thumb();
        return executed(false, cycles + 1U);
    }

    // Format 9: immediate-offset word and byte transfers.
    if ((instruction & 0xE000U) == 0x6000U) {
        if (!bus) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const bool byte_transfer = bit(instruction, 12);
        const bool load = bit(instruction, 11);
        const auto immediate = static_cast<std::uint32_t>((instruction >> 6U) & 0x1FU);
        const auto offset = byte_transfer ? immediate : immediate << 2U;
        const auto base_register = static_cast<std::size_t>((instruction >> 3U) & 0x7U);
        const auto data_register = static_cast<std::size_t>(instruction & 0x7U);
        const auto address = register_value(base_register) + offset;
        const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, program_counter_};

        if (load) {
            const auto read =
                byte_transfer ? bus->read8(address, access) : bus->read32(address, access);
            set_register(data_register, read.value);
            advance_thumb();
            return executed(false, read.cycles + 1U);
        }
        const auto value = register_value(data_register);
        const auto write = byte_transfer
                               ? bus->write8(address, static_cast<std::uint8_t>(value), access)
                               : bus->write32(address, value, access);
        advance_thumb();
        return executed(false, write.cycles);
    }

    // Format 10: immediate-offset halfword transfer.
    if ((instruction & 0xF000U) == 0x8000U) {
        if (!bus) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const bool load = bit(instruction, 11);
        const auto offset = static_cast<std::uint32_t>((instruction >> 6U) & 0x1FU) << 1U;
        const auto base_register = static_cast<std::size_t>((instruction >> 3U) & 0x7U);
        const auto data_register = static_cast<std::size_t>(instruction & 0x7U);
        const auto address = register_value(base_register) + offset;
        const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, program_counter_};
        if (load) {
            const auto read = bus->read16(address, access);
            set_register(data_register, read.value);
            advance_thumb();
            return executed(false, read.cycles + 1U);
        }
        const auto write = bus->write16(
            address, static_cast<std::uint16_t>(register_value(data_register)), access);
        advance_thumb();
        return executed(false, write.cycles);
    }

    // Format 11: SP-relative word transfer.
    if ((instruction & 0xF000U) == 0x9000U) {
        if (!bus) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const bool load = bit(instruction, 11);
        const auto data_register = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const auto address =
            register_value(kStackPointer) + (static_cast<std::uint32_t>(instruction & 0xFFU) << 2U);
        const BusAccess access{AccessSequence::NonSequential, AccessKind::Data, program_counter_};
        if (load) {
            const auto read = bus->read32(address, access);
            set_register(data_register, read.value);
            advance_thumb();
            return executed(false, read.cycles + 1U);
        }
        const auto write = bus->write32(address, register_value(data_register), access);
        advance_thumb();
        return executed(false, write.cycles);
    }

    // Format 12: form an address relative to PC or SP.
    if ((instruction & 0xF000U) == 0xA000U) {
        const bool use_stack_pointer = bit(instruction, 11);
        const auto destination = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const auto offset = static_cast<std::uint32_t>(instruction & 0xFFU) << 2U;
        const auto base =
            use_stack_pointer ? register_value(kStackPointer) : (program_counter_ + 4U) & ~3U;
        set_register(destination, base + offset);
        advance_thumb();
        return executed();
    }

    // Format 13: add or subtract an immediate from SP.
    if ((instruction & 0xFF00U) == 0xB000U) {
        const auto offset = static_cast<std::uint32_t>(instruction & 0x7FU) << 2U;
        const auto stack_pointer = register_value(kStackPointer);
        set_register(kStackPointer,
                     bit(instruction, 7) ? stack_pointer - offset : stack_pointer + offset);
        advance_thumb();
        return executed();
    }

    // Format 14: PUSH and POP.
    if ((instruction & 0xF600U) == 0xB400U) {
        if (!bus) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const bool load = bit(instruction, 11);
        const bool include_extra_register = bit(instruction, 8);
        const auto register_list = static_cast<std::uint8_t>(instruction);
        const auto register_count = static_cast<std::uint32_t>(std::popcount(register_list)) +
                                    static_cast<std::uint32_t>(include_extra_register);
        if (register_count == 0U) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }

        auto address = register_value(kStackPointer);
        if (!load) {
            address -= register_count * 4U;
            set_register(kStackPointer, address);
        }

        std::uint32_t cycles = 0;
        bool first_access = true;
        for (std::size_t register_index = 0; register_index < 8U; ++register_index) {
            if (!bit(register_list, static_cast<unsigned>(register_index))) {
                continue;
            }
            const BusAccess access{
                first_access ? AccessSequence::NonSequential : AccessSequence::Sequential,
                AccessKind::Data,
                program_counter_,
            };
            first_access = false;
            if (load) {
                const auto read = bus->read32(address, access);
                cycles += read.cycles;
                set_register(register_index, read.value);
            } else {
                cycles += bus->write32(address, register_value(register_index), access).cycles;
            }
            address += 4U;
        }

        bool pipeline_flushed = false;
        if (include_extra_register) {
            const BusAccess access{
                first_access ? AccessSequence::NonSequential : AccessSequence::Sequential,
                AccessKind::Data,
                program_counter_,
            };
            if (load) {
                const auto read = bus->read32(address, access);
                cycles += read.cycles;
                branch_to(read.value);
                pipeline_flushed = true;
            } else {
                cycles += bus->write32(address, register_value(kLinkRegister), access).cycles;
            }
            address += 4U;
        }

        if (load) {
            set_register(kStackPointer, address);
        }
        if (!pipeline_flushed) {
            advance_thumb();
        }
        return executed(pipeline_flushed, cycles + static_cast<std::uint32_t>(load));
    }

    // Format 15: multiple load/store of low registers.
    if ((instruction & 0xF000U) == 0xC000U) {
        if (!bus) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const bool load = bit(instruction, 11);
        const auto base_register = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const auto register_list = static_cast<std::uint8_t>(instruction);
        const auto register_count = static_cast<std::uint32_t>(std::popcount(register_list));
        if (register_list == 0U ||
            (load && bit(register_list, static_cast<unsigned>(base_register)))) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }

        auto address = register_value(base_register);
        std::uint32_t cycles = 0;
        bool first_access = true;
        for (std::size_t register_index = 0; register_index < 8U; ++register_index) {
            if (!bit(register_list, static_cast<unsigned>(register_index))) {
                continue;
            }
            const BusAccess access{
                first_access ? AccessSequence::NonSequential : AccessSequence::Sequential,
                AccessKind::Data,
                program_counter_,
            };
            first_access = false;
            if (load) {
                const auto read = bus->read32(address, access);
                cycles += read.cycles;
                set_register(register_index, read.value);
            } else {
                cycles += bus->write32(address, register_value(register_index), access).cycles;
            }
            address += 4U;
        }
        set_register(base_register, register_value(base_register) + register_count * 4U);
        advance_thumb();
        return executed(false, cycles + static_cast<std::uint32_t>(load));
    }

    // Format 4: ALU operations on low registers.
    if ((instruction & 0xFC00U) == 0x4000U) {
        const auto operation = static_cast<std::uint8_t>((instruction >> 6U) & 0xFU);
        const auto source = static_cast<std::size_t>((instruction >> 3U) & 0x7U);
        const auto destination = static_cast<std::size_t>(instruction & 0x7U);
        const auto left = register_value(destination);
        const auto right = register_value(source);
        std::uint32_t value = 0;
        bool write_result = true;

        switch (operation) {
        case 0x0: // AND
            value = left & right;
            set_nz(value);
            break;
        case 0x1: // EOR
            value = left ^ right;
            set_nz(value);
            break;
        case 0x2: { // LSL
            const auto shifted =
                shift_by_register(ShiftType::LogicalLeft, left,
                                  static_cast<std::uint8_t>(right & 0xFFU), cpsr_.carry());
            value = shifted.value;
            set_logical_flags(value, shifted.carry);
            break;
        }
        case 0x3: { // LSR
            const auto shifted =
                shift_by_register(ShiftType::LogicalRight, left,
                                  static_cast<std::uint8_t>(right & 0xFFU), cpsr_.carry());
            value = shifted.value;
            set_logical_flags(value, shifted.carry);
            break;
        }
        case 0x4: { // ASR
            const auto shifted =
                shift_by_register(ShiftType::ArithmeticRight, left,
                                  static_cast<std::uint8_t>(right & 0xFFU), cpsr_.carry());
            value = shifted.value;
            set_logical_flags(value, shifted.carry);
            break;
        }
        case 0x5: { // ADC
            const auto result = add(left, right, cpsr_.carry());
            value = result.value;
            set_arithmetic_flags(result);
            break;
        }
        case 0x6: { // SBC
            const auto result = subtract(left, right, !cpsr_.carry());
            value = result.value;
            set_arithmetic_flags(result);
            break;
        }
        case 0x7: { // ROR
            const auto shifted =
                shift_by_register(ShiftType::RotateRight, left,
                                  static_cast<std::uint8_t>(right & 0xFFU), cpsr_.carry());
            value = shifted.value;
            set_logical_flags(value, shifted.carry);
            break;
        }
        case 0x8: // TST
            value = left & right;
            set_nz(value);
            write_result = false;
            break;
        case 0x9: { // NEG
            const auto result = subtract(0, right, false);
            value = result.value;
            set_arithmetic_flags(result);
            break;
        }
        case 0xA: // CMP
            set_arithmetic_flags(subtract(left, right, false));
            write_result = false;
            break;
        case 0xB: // CMN
            set_arithmetic_flags(add(left, right, false));
            write_result = false;
            break;
        case 0xC: // ORR
            value = left | right;
            set_nz(value);
            break;
        case 0xD: // MUL
            value = static_cast<std::uint32_t>(static_cast<std::uint64_t>(left) * right);
            set_nz(value);
            break;
        case 0xE: // BIC
            value = left & ~right;
            set_nz(value);
            break;
        case 0xF: // MVN
            value = ~right;
            set_nz(value);
            break;
        default:
            return status(ExecutionStatus::UnsupportedInstruction);
        }

        if (write_result) {
            set_register(destination, value);
        }
        advance_thumb();
        return executed();
    }

    // Format 3: move, compare, add, and subtract an eight-bit immediate.
    if ((instruction & 0xE000U) == 0x2000U) {
        const auto operation = static_cast<std::uint8_t>((instruction >> 11U) & 0x3U);
        const auto destination = static_cast<std::size_t>((instruction >> 8U) & 0x7U);
        const auto immediate = static_cast<std::uint32_t>(instruction & 0xFFU);
        const auto original = register_value(destination);

        switch (operation) {
        case 0x0: // MOV
            set_register(destination, immediate);
            set_nz(immediate);
            break;
        case 0x1: // CMP
            set_arithmetic_flags(subtract(original, immediate, false));
            break;
        case 0x2: { // ADD
            const auto result = add(original, immediate, false);
            set_register(destination, result.value);
            set_arithmetic_flags(result);
            break;
        }
        case 0x3: { // SUB
            const auto result = subtract(original, immediate, false);
            set_register(destination, result.value);
            set_arithmetic_flags(result);
            break;
        }
        default:
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        advance_thumb();
        return executed();
    }

    // Format 2: add or subtract a register or three-bit immediate.
    if ((instruction & 0xF800U) == 0x1800U) {
        const auto immediate_operand = (instruction & 0x0400U) != 0U;
        const auto subtract_operation = (instruction & 0x0200U) != 0U;
        const auto operand_field = static_cast<std::size_t>((instruction >> 6U) & 0x7U);
        const auto source = static_cast<std::size_t>((instruction >> 3U) & 0x7U);
        const auto destination = static_cast<std::size_t>(instruction & 0x7U);
        const auto right = immediate_operand ? static_cast<std::uint32_t>(operand_field)
                                             : register_value(operand_field);
        const auto result = subtract_operation ? subtract(register_value(source), right, false)
                                               : add(register_value(source), right, false);
        set_register(destination, result.value);
        set_arithmetic_flags(result);
        advance_thumb();
        return executed();
    }

    // Format 1: immediate shifts between low registers.
    if ((instruction & 0xE000U) == 0x0000U) {
        const auto operation = static_cast<std::uint8_t>((instruction >> 11U) & 0x3U);
        if (operation == 0x3U) {
            return status(ExecutionStatus::UnsupportedInstruction);
        }
        const auto amount = static_cast<std::uint8_t>((instruction >> 6U) & 0x1FU);
        const auto source = static_cast<std::size_t>((instruction >> 3U) & 0x7U);
        const auto destination = static_cast<std::size_t>(instruction & 0x7U);
        const auto shifted = shift_by_immediate(static_cast<ShiftType>(operation),
                                                register_value(source), amount, cpsr_.carry());
        set_register(destination, shifted.value);
        set_logical_flags(shifted.value, shifted.carry);
        advance_thumb();
        return executed();
    }

    return status(ExecutionStatus::UnsupportedInstruction);
}

void Arm7Tdmi::take_exception(const ExceptionType exception) noexcept {
    if (exception == ExceptionType::Reset) {
        reset();
        return;
    }

    const auto previous_status = cpsr_;
    ProcessorMode target_mode = ProcessorMode::Supervisor;
    std::uint32_t vector = 0;
    std::uint32_t return_address = program_counter_;

    switch (exception) {
    case ExceptionType::Reset:
        break;
    case ExceptionType::UndefinedInstruction:
        target_mode = ProcessorMode::Undefined;
        vector = 0x04U;
        return_address += previous_status.instruction_set() == InstructionSet::Thumb ? 2U : 4U;
        break;
    case ExceptionType::SoftwareInterrupt:
        target_mode = ProcessorMode::Supervisor;
        vector = 0x08U;
        return_address += previous_status.instruction_set() == InstructionSet::Thumb ? 2U : 4U;
        break;
    case ExceptionType::PrefetchAbort:
        target_mode = ProcessorMode::Abort;
        vector = 0x0CU;
        return_address += 4U;
        break;
    case ExceptionType::DataAbort:
        target_mode = ProcessorMode::Abort;
        vector = 0x10U;
        return_address += 8U;
        break;
    case ExceptionType::Irq:
        target_mode = ProcessorMode::Irq;
        vector = 0x18U;
        return_address += 4U;
        break;
    case ExceptionType::Fiq:
        target_mode = ProcessorMode::Fiq;
        vector = 0x1CU;
        return_address += 4U;
        break;
    }

    auto* target_spsr = spsr(target_mode);
    assert(target_spsr != nullptr);
    *target_spsr = previous_status;

    cpsr_.set_mode(target_mode);
    cpsr_.set_instruction_set(InstructionSet::Arm);
    cpsr_.set_irq_disabled(true);
    if (exception == ExceptionType::Fiq) {
        cpsr_.set_fiq_disabled(true);
    }

    set_register(kLinkRegister, return_address);
    branch_to(vector);
}

bool Arm7Tdmi::try_take_irq() noexcept {
    if (cpsr_.irq_disabled()) {
        return false;
    }
    take_exception(ExceptionType::Irq);
    return true;
}

bool Arm7Tdmi::try_take_fiq() noexcept {
    if (cpsr_.fiq_disabled()) {
        return false;
    }
    take_exception(ExceptionType::Fiq);
    return true;
}

} // namespace srgba::core
