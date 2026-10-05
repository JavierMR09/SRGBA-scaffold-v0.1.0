#pragma once

#include "srgba/core/arm7tdmi.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace srgba::homebrew {

using core::Condition;
using core::ShiftType;

// A deliberately small ARM-state assembler used to build SRGBA's test and demo ROMs from C++
// without an external toolchain. It covers only the encodings those programs need.
class ArmAssembler {
  public:
    enum class Opcode : std::uint8_t {
        And = 0x0,
        Eor = 0x1,
        Sub = 0x2,
        Rsb = 0x3,
        Add = 0x4,
        Adc = 0x5,
        Sbc = 0x6,
        Rsc = 0x7,
        Tst = 0x8,
        Teq = 0x9,
        Cmp = 0xA,
        Cmn = 0xB,
        Orr = 0xC,
        Mov = 0xD,
        Bic = 0xE,
        Mvn = 0xF,
    };

    static constexpr unsigned kSp = 13;
    static constexpr unsigned kLr = 14;
    static constexpr unsigned kPc = 15;

    explicit ArmAssembler(std::uint32_t base_address);

    [[nodiscard]] std::uint32_t here() const noexcept;
    void label(const std::string& name);
    [[nodiscard]] std::uint32_t address_of(const std::string& name) const;
    void word(std::uint32_t value);
    void align(std::size_t alignment);

    // Data processing. Immediates must be encodable as an 8-bit value rotated by an even amount.
    void data_imm(Opcode opcode, unsigned rd, unsigned rn, std::uint32_t immediate,
                  Condition condition = Condition::Always, bool set_flags = false);
    void data_reg(Opcode opcode, unsigned rd, unsigned rn, unsigned rm,
                  ShiftType shift = ShiftType::LogicalLeft, unsigned amount = 0,
                  Condition condition = Condition::Always, bool set_flags = false);

    void mov_imm(unsigned rd, std::uint32_t immediate, Condition condition = Condition::Always);
    void mov(unsigned rd, unsigned rm, ShiftType shift = ShiftType::LogicalLeft,
             unsigned amount = 0, Condition condition = Condition::Always);
    void add_imm(unsigned rd, unsigned rn, std::uint32_t immediate,
                 Condition condition = Condition::Always);
    void sub_imm(unsigned rd, unsigned rn, std::uint32_t immediate,
                 Condition condition = Condition::Always, bool set_flags = false);
    void add(unsigned rd, unsigned rn, unsigned rm, ShiftType shift = ShiftType::LogicalLeft,
             unsigned amount = 0);
    void cmp_imm(unsigned rn, std::uint32_t immediate, Condition condition = Condition::Always);
    void cmp(unsigned rn, unsigned rm);
    void tst_imm(unsigned rn, std::uint32_t immediate);
    void orr_imm(unsigned rd, unsigned rn, std::uint32_t immediate);
    void and_imm(unsigned rd, unsigned rn, std::uint32_t immediate);
    void mul(unsigned rd, unsigned rm, unsigned rs);

    // Loads any 32-bit constant through a PC-relative literal pool.
    void load_constant(unsigned rd, std::uint32_t value);
    void load_address(unsigned rd, const std::string& label_name);

    // Immediate-offset transfers.
    void ldr(unsigned rd, unsigned rn, std::int32_t offset = 0);
    void str(unsigned rd, unsigned rn, std::int32_t offset = 0);
    void ldrb(unsigned rd, unsigned rn, std::int32_t offset = 0);
    void strb(unsigned rd, unsigned rn, std::int32_t offset = 0);
    void ldrh(unsigned rd, unsigned rn, std::int32_t offset = 0);
    void strh(unsigned rd, unsigned rn, std::int32_t offset = 0,
              Condition condition = Condition::Always);
    // STRH Rd, [Rn], #offset (post-indexed with write-back).
    void strh_post(unsigned rd, unsigned rn, std::int32_t offset);

    void push(std::initializer_list<unsigned> registers);
    void pop(std::initializer_list<unsigned> registers);

    void b(const std::string& target, Condition condition = Condition::Always);
    void bl(const std::string& target, Condition condition = Condition::Always);
    void bx(unsigned rm, Condition condition = Condition::Always);
    void swi(std::uint8_t number);

    // Emits the pending literal pool at the current position (call between functions).
    void literal_pool();

    // Resolves branches and literals, returning the assembled bytes.
    [[nodiscard]] std::vector<std::uint8_t> finish();

    [[nodiscard]] static std::optional<std::uint32_t>
    encode_immediate(std::uint32_t value) noexcept;

  private:
    struct BranchFixup {
        std::size_t index;
        std::string target;
    };
    struct LiteralFixup {
        std::size_t index;
        std::uint32_t value;
        std::optional<std::string> label;
    };

    void transfer(bool load, bool byte, unsigned rd, unsigned rn, std::int32_t offset);
    void halfword_transfer(bool load, unsigned rd, unsigned rn, std::int32_t offset,
                           Condition condition, bool pre_indexed);
    void branch(const std::string& target, Condition condition, bool link);
    [[nodiscard]] static std::uint32_t register_list(std::initializer_list<unsigned> registers);
    [[nodiscard]] static std::uint32_t cond(Condition condition) noexcept;

    std::uint32_t base_address_;
    std::vector<std::uint32_t> words_;
    std::map<std::string, std::uint32_t> labels_;
    std::vector<BranchFixup> branch_fixups_;
    std::vector<LiteralFixup> pending_literals_;
    std::vector<BranchFixup> address_slots_; // literal pool words that hold label addresses
};

} // namespace srgba::homebrew
