#include "arm_assembler.hpp"

#include <bit>
#include <stdexcept>

namespace srgba::homebrew {

ArmAssembler::ArmAssembler(const std::uint32_t base_address) : base_address_(base_address) {}

std::uint32_t ArmAssembler::here() const noexcept {
    return base_address_ + static_cast<std::uint32_t>(words_.size() * 4U);
}

void ArmAssembler::label(const std::string& name) {
    if (!labels_.emplace(name, here()).second) {
        throw std::invalid_argument("Duplicate label: " + name);
    }
}

std::uint32_t ArmAssembler::address_of(const std::string& name) const {
    const auto found = labels_.find(name);
    if (found == labels_.end()) {
        throw std::invalid_argument("Unknown label: " + name);
    }
    return found->second;
}

void ArmAssembler::word(const std::uint32_t value) {
    words_.push_back(value);
}

void ArmAssembler::align(const std::size_t alignment) {
    while ((words_.size() * 4U) % alignment != 0U) {
        words_.push_back(0);
    }
}

std::uint32_t ArmAssembler::cond(const Condition condition) noexcept {
    return static_cast<std::uint32_t>(condition) << 28U;
}

std::optional<std::uint32_t> ArmAssembler::encode_immediate(const std::uint32_t value) noexcept {
    for (std::uint32_t rotation = 0; rotation < 16U; ++rotation) {
        const auto unrotated = std::rotl(value, static_cast<int>(rotation * 2U));
        if (unrotated <= 0xFFU) {
            return (rotation << 8U) | unrotated;
        }
    }
    return std::nullopt;
}

void ArmAssembler::data_imm(const Opcode opcode, const unsigned rd, const unsigned rn,
                            const std::uint32_t immediate, const Condition condition,
                            const bool set_flags) {
    const auto encoded = encode_immediate(immediate);
    if (!encoded) {
        throw std::invalid_argument("Immediate cannot be encoded as a rotated 8-bit value.");
    }
    const auto op = static_cast<std::uint32_t>(opcode);
    const bool test = op >= 0x8U && op <= 0xBU;
    words_.push_back(cond(condition) | 0x02000000U | (op << 21U) |
                     (static_cast<std::uint32_t>(set_flags || test) << 20U) | (rn << 16U) |
                     (rd << 12U) | *encoded);
}

void ArmAssembler::data_reg(const Opcode opcode, const unsigned rd, const unsigned rn,
                            const unsigned rm, const ShiftType shift, const unsigned amount,
                            const Condition condition, const bool set_flags) {
    const auto op = static_cast<std::uint32_t>(opcode);
    const bool test = op >= 0x8U && op <= 0xBU;
    words_.push_back(cond(condition) | (op << 21U) |
                     (static_cast<std::uint32_t>(set_flags || test) << 20U) | (rn << 16U) |
                     (rd << 12U) | ((amount & 0x1FU) << 7U) |
                     (static_cast<std::uint32_t>(shift) << 5U) | rm);
}

void ArmAssembler::mov_imm(const unsigned rd, const std::uint32_t immediate,
                           const Condition condition) {
    data_imm(Opcode::Mov, rd, 0, immediate, condition);
}

void ArmAssembler::mov(const unsigned rd, const unsigned rm, const ShiftType shift,
                       const unsigned amount, const Condition condition) {
    data_reg(Opcode::Mov, rd, 0, rm, shift, amount, condition);
}

void ArmAssembler::add_imm(const unsigned rd, const unsigned rn, const std::uint32_t immediate,
                           const Condition condition) {
    data_imm(Opcode::Add, rd, rn, immediate, condition);
}

void ArmAssembler::sub_imm(const unsigned rd, const unsigned rn, const std::uint32_t immediate,
                           const Condition condition, const bool set_flags) {
    data_imm(Opcode::Sub, rd, rn, immediate, condition, set_flags);
}

void ArmAssembler::add(const unsigned rd, const unsigned rn, const unsigned rm,
                       const ShiftType shift, const unsigned amount) {
    data_reg(Opcode::Add, rd, rn, rm, shift, amount);
}

void ArmAssembler::cmp_imm(const unsigned rn, const std::uint32_t immediate,
                           const Condition condition) {
    data_imm(Opcode::Cmp, 0, rn, immediate, condition);
}

void ArmAssembler::cmp(const unsigned rn, const unsigned rm) {
    data_reg(Opcode::Cmp, 0, rn, rm);
}

void ArmAssembler::tst_imm(const unsigned rn, const std::uint32_t immediate) {
    data_imm(Opcode::Tst, 0, rn, immediate);
}

void ArmAssembler::orr_imm(const unsigned rd, const unsigned rn, const std::uint32_t immediate) {
    data_imm(Opcode::Orr, rd, rn, immediate);
}

void ArmAssembler::and_imm(const unsigned rd, const unsigned rn, const std::uint32_t immediate) {
    data_imm(Opcode::And, rd, rn, immediate);
}

void ArmAssembler::mul(const unsigned rd, const unsigned rm, const unsigned rs) {
    words_.push_back(0xE0000090U | (rd << 16U) | (rs << 8U) | rm);
}

void ArmAssembler::load_constant(const unsigned rd, const std::uint32_t value) {
    if (const auto encoded = encode_immediate(value)) {
        mov_imm(rd, value);
        return;
    }
    pending_literals_.push_back({words_.size(), value, std::nullopt});
    words_.push_back(0xE59F0000U | (rd << 12U)); // LDR rd, [pc, #offset] (patched later)
}

void ArmAssembler::load_address(const unsigned rd, const std::string& label_name) {
    pending_literals_.push_back({words_.size(), 0, label_name});
    words_.push_back(0xE59F0000U | (rd << 12U));
}

void ArmAssembler::transfer(const bool load, const bool byte, const unsigned rd, const unsigned rn,
                            const std::int32_t offset) {
    const bool add_offset = offset >= 0;
    const auto magnitude = static_cast<std::uint32_t>(add_offset ? offset : -offset);
    if (magnitude > 0xFFFU) {
        throw std::invalid_argument("Transfer offset out of range.");
    }
    words_.push_back(0xE5000000U | (static_cast<std::uint32_t>(add_offset) << 23U) |
                     (static_cast<std::uint32_t>(byte) << 22U) |
                     (static_cast<std::uint32_t>(load) << 20U) | (rn << 16U) | (rd << 12U) |
                     magnitude);
}

void ArmAssembler::halfword_transfer(const bool load, const unsigned rd, const unsigned rn,
                                     const std::int32_t offset, const Condition condition,
                                     const bool pre_indexed) {
    const bool add_offset = offset >= 0;
    const auto magnitude = static_cast<std::uint32_t>(add_offset ? offset : -offset);
    if (magnitude > 0xFFU) {
        throw std::invalid_argument("Halfword transfer offset out of range.");
    }
    // Post-indexed transfers always write back; W must be clear.
    words_.push_back(cond(condition) | 0x004000B0U |
                     (static_cast<std::uint32_t>(pre_indexed) << 24U) |
                     (static_cast<std::uint32_t>(add_offset) << 23U) |
                     (static_cast<std::uint32_t>(load) << 20U) | (rn << 16U) | (rd << 12U) |
                     ((magnitude & 0xF0U) << 4U) | (magnitude & 0x0FU));
}

void ArmAssembler::ldr(const unsigned rd, const unsigned rn, const std::int32_t offset) {
    transfer(true, false, rd, rn, offset);
}

void ArmAssembler::str(const unsigned rd, const unsigned rn, const std::int32_t offset) {
    transfer(false, false, rd, rn, offset);
}

void ArmAssembler::ldrb(const unsigned rd, const unsigned rn, const std::int32_t offset) {
    transfer(true, true, rd, rn, offset);
}

void ArmAssembler::strb(const unsigned rd, const unsigned rn, const std::int32_t offset) {
    transfer(false, true, rd, rn, offset);
}

void ArmAssembler::ldrh(const unsigned rd, const unsigned rn, const std::int32_t offset) {
    halfword_transfer(true, rd, rn, offset, Condition::Always, true);
}

void ArmAssembler::strh(const unsigned rd, const unsigned rn, const std::int32_t offset,
                        const Condition condition) {
    halfword_transfer(false, rd, rn, offset, condition, true);
}

void ArmAssembler::strh_post(const unsigned rd, const unsigned rn, const std::int32_t offset) {
    halfword_transfer(false, rd, rn, offset, Condition::Always, false);
}

std::uint32_t ArmAssembler::register_list(const std::initializer_list<unsigned> registers) {
    std::uint32_t list = 0;
    for (const auto index : registers) {
        list |= 1U << index;
    }
    return list;
}

void ArmAssembler::push(const std::initializer_list<unsigned> registers) {
    words_.push_back(0xE92D0000U | register_list(registers)); // STMFD sp!, {...}
}

void ArmAssembler::pop(const std::initializer_list<unsigned> registers) {
    words_.push_back(0xE8BD0000U | register_list(registers)); // LDMFD sp!, {...}
}

void ArmAssembler::branch(const std::string& target, const Condition condition, const bool link) {
    branch_fixups_.push_back({words_.size(), target});
    words_.push_back(cond(condition) | 0x0A000000U | (static_cast<std::uint32_t>(link) << 24U));
}

void ArmAssembler::b(const std::string& target, const Condition condition) {
    branch(target, condition, false);
}

void ArmAssembler::bl(const std::string& target, const Condition condition) {
    branch(target, condition, true);
}

void ArmAssembler::bx(const unsigned rm, const Condition condition) {
    words_.push_back(cond(condition) | 0x012FFF10U | rm);
}

void ArmAssembler::swi(const std::uint8_t number) {
    // ARM-state BIOS calls place the function number in bits 16-23 of the comment field.
    words_.push_back(0xEF000000U | (static_cast<std::uint32_t>(number) << 16U));
}

void ArmAssembler::literal_pool() {
    for (const auto& literal : pending_literals_) {
        if (literal.label) {
            address_slots_.push_back({words_.size(), *literal.label});
        }
        const auto value = literal.value;
        const auto pool_address = here();
        const auto instruction_address =
            base_address_ + static_cast<std::uint32_t>(literal.index * 4U);
        const auto offset = pool_address - (instruction_address + 8U);
        if (offset > 0xFFFU) {
            throw std::invalid_argument("Literal pool is out of range.");
        }
        words_[literal.index] |= offset;
        words_.push_back(value);
    }
    pending_literals_.clear();
}

std::vector<std::uint8_t> ArmAssembler::finish() {
    literal_pool();
    for (const auto& fixup : branch_fixups_) {
        const auto target = address_of(fixup.target);
        const auto source = base_address_ + static_cast<std::uint32_t>(fixup.index * 4U);
        const auto offset =
            static_cast<std::int64_t>(target) - (static_cast<std::int64_t>(source) + 8);
        words_[fixup.index] |= static_cast<std::uint32_t>(offset >> 2) & 0x00FFFFFFU;
    }
    branch_fixups_.clear();
    for (const auto& slot : address_slots_) {
        words_[slot.index] = address_of(slot.target);
    }
    address_slots_.clear();

    std::vector<std::uint8_t> bytes;
    bytes.reserve(words_.size() * 4U);
    for (const auto value : words_) {
        for (std::size_t index = 0; index < 4U; ++index) {
            bytes.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
        }
    }
    return bytes;
}

} // namespace srgba::homebrew
