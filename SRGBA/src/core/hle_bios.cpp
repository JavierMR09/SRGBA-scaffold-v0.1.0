#include "srgba/core/hle_bios.hpp"

#include "srgba/core/arm7tdmi.hpp"
#include "srgba/core/gba_bus.hpp"
#include "srgba/core/system_bios.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <vector>

namespace srgba::core {
namespace {

// BIOS-internal accesses see the BIOS as readable, as they do on hardware.
constexpr BusAccess kBiosAccess{AccessSequence::NonSequential, AccessKind::Data, 0U};

constexpr std::uint32_t kBiosChecksum = 0xBAAE187FU;

class Memory {
  public:
    explicit Memory(GbaBus& bus) noexcept : bus_(bus) {}

    [[nodiscard]] std::uint8_t read8(const std::uint32_t address) noexcept {
        return static_cast<std::uint8_t>(bus_.read8(address, kBiosAccess).value);
    }
    [[nodiscard]] std::uint16_t read16(const std::uint32_t address) noexcept {
        return static_cast<std::uint16_t>(bus_.read16(address & ~1U, kBiosAccess).value);
    }
    [[nodiscard]] std::uint32_t read32(const std::uint32_t address) noexcept {
        return bus_.read32(address & ~3U, kBiosAccess).value;
    }
    void write8(const std::uint32_t address, const std::uint8_t value) noexcept {
        static_cast<void>(bus_.write8(address, value, kBiosAccess));
    }
    void write16(const std::uint32_t address, const std::uint16_t value) noexcept {
        static_cast<void>(bus_.write16(address & ~1U, value, kBiosAccess));
    }
    void write32(const std::uint32_t address, const std::uint32_t value) noexcept {
        static_cast<void>(bus_.write32(address & ~3U, value, kBiosAccess));
    }

  private:
    GbaBus& bus_;
};

enum class Destination {
    Wram, // byte writes
    Vram, // halfword writes
};

void write_output(Memory& memory, const std::uint32_t destination,
                  const std::vector<std::uint8_t>& bytes, const Destination kind) noexcept {
    if (kind == Destination::Wram) {
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            memory.write8(destination + static_cast<std::uint32_t>(index), bytes[index]);
        }
        return;
    }
    for (std::size_t index = 0; index < bytes.size(); index += 2U) {
        const auto high = index + 1U < bytes.size() ? bytes[index + 1U] : std::uint8_t{0};
        memory.write16(destination + static_cast<std::uint32_t>(index),
                       static_cast<std::uint16_t>(bytes[index] | (high << 8U)));
    }
}

// Prevents a corrupt header from asking for an absurd amount of host memory.
constexpr std::uint32_t kMaxDecompressedSize = 0x01000000U;

[[nodiscard]] std::uint32_t lz77_uncompress(Memory& memory, std::uint32_t source,
                                            const std::uint32_t destination,
                                            const Destination kind) {
    const auto header = memory.read32(source);
    const auto size = std::min(header >> 8U, kMaxDecompressedSize);
    source += 4U;
    std::vector<std::uint8_t> output;
    output.reserve(size);
    while (output.size() < size) {
        const auto flags = memory.read8(source++);
        for (int block = 7; block >= 0 && output.size() < size; --block) {
            if (((flags >> block) & 1U) == 0U) {
                output.push_back(memory.read8(source++));
                continue;
            }
            const auto first = memory.read8(source++);
            const auto second = memory.read8(source++);
            const auto length = static_cast<std::size_t>((first >> 4U) + 3U);
            const auto displacement =
                static_cast<std::size_t>((((first & 0x0FU) << 8U) | second) + 1U);
            for (std::size_t copy = 0; copy < length && output.size() < size; ++copy) {
                output.push_back(displacement <= output.size()
                                     ? output[output.size() - displacement]
                                     : std::uint8_t{0});
            }
        }
    }
    write_output(memory, destination, output, kind);
    return static_cast<std::uint32_t>(size);
}

[[nodiscard]] std::uint32_t rl_uncompress(Memory& memory, std::uint32_t source,
                                          const std::uint32_t destination, const Destination kind) {
    const auto header = memory.read32(source);
    const auto size = std::min(header >> 8U, kMaxDecompressedSize);
    source += 4U;
    std::vector<std::uint8_t> output;
    output.reserve(size);
    while (output.size() < size) {
        const auto flag = memory.read8(source++);
        if ((flag & 0x80U) != 0U) {
            const auto length = static_cast<std::size_t>((flag & 0x7FU) + 3U);
            const auto value = memory.read8(source++);
            for (std::size_t copy = 0; copy < length && output.size() < size; ++copy) {
                output.push_back(value);
            }
        } else {
            const auto length = static_cast<std::size_t>((flag & 0x7FU) + 1U);
            for (std::size_t copy = 0; copy < length && output.size() < size; ++copy) {
                output.push_back(memory.read8(source++));
            }
        }
    }
    write_output(memory, destination, output, kind);
    return static_cast<std::uint32_t>(size);
}

[[nodiscard]] std::uint32_t huffman_uncompress(Memory& memory, const std::uint32_t source,
                                               std::uint32_t destination) {
    const auto header = memory.read32(source);
    const auto bits_per_value = header & 0x0FU;
    const auto size = std::min(header >> 8U, kMaxDecompressedSize);
    if (bits_per_value != 4U && bits_per_value != 8U) {
        return 0;
    }
    const auto tree_size = memory.read8(source + 4U);
    const auto root_address = source + 5U;
    auto data = source + 4U + (static_cast<std::uint32_t>(tree_size) + 1U) * 2U;

    std::uint32_t written = 0;
    std::uint32_t pending = 0;
    std::uint32_t pending_bits = 0;
    auto node_address = root_address;
    auto node = memory.read8(node_address);
    while (written < size) {
        const auto word = memory.read32(data);
        data += 4U;
        for (int bit_index = 31; bit_index >= 0 && written < size; --bit_index) {
            const auto direction = (word >> static_cast<unsigned>(bit_index)) & 1U;
            const auto child = (node_address & ~1U) +
                               static_cast<std::uint32_t>(node & 0x3FU) * 2U + 2U + direction;
            const bool leaf = direction != 0U ? (node & 0x40U) != 0U : (node & 0x80U) != 0U;
            if (!leaf) {
                node_address = child;
                node = memory.read8(node_address);
                continue;
            }
            const auto value = memory.read8(child) & ((1U << bits_per_value) - 1U);
            pending |= static_cast<std::uint32_t>(value) << pending_bits;
            pending_bits += bits_per_value;
            if (pending_bits == 32U) {
                memory.write32(destination, pending);
                destination += 4U;
                written += 4U;
                pending = 0;
                pending_bits = 0;
            }
            node_address = root_address;
            node = memory.read8(node_address);
        }
    }
    return written;
}

void diff_unfilter(Memory& memory, std::uint32_t source, const std::uint32_t destination,
                   const bool sixteen_bit, const Destination kind) {
    const auto header = memory.read32(source);
    const auto size = std::min(header >> 8U, kMaxDecompressedSize);
    source += 4U;
    std::vector<std::uint8_t> output;
    output.reserve(size);
    if (sixteen_bit) {
        std::uint16_t value = 0;
        for (std::uint32_t offset = 0; offset + 1U < size; offset += 2U) {
            value = static_cast<std::uint16_t>(value + memory.read16(source + offset));
            output.push_back(static_cast<std::uint8_t>(value));
            output.push_back(static_cast<std::uint8_t>(value >> 8U));
        }
    } else {
        std::uint8_t value = 0;
        for (std::uint32_t offset = 0; offset < size; ++offset) {
            value = static_cast<std::uint8_t>(value + memory.read8(source + offset));
            output.push_back(value);
        }
    }
    write_output(memory, destination, output, kind);
}

void bit_unpack(Memory& memory, std::uint32_t source, std::uint32_t destination,
                const std::uint32_t info) noexcept {
    const auto source_length = memory.read16(info);
    const auto source_width = memory.read8(info + 2U);
    const auto destination_width = memory.read8(info + 3U);
    const auto data_offset_word = memory.read32(info + 4U);
    const auto data_offset = data_offset_word & 0x7FFFFFFFU;
    const bool offset_zero_values = (data_offset_word & 0x80000000U) != 0U;
    const auto valid_width = [](const std::uint8_t width) {
        return width == 1U || width == 2U || width == 4U || width == 8U || width == 16U ||
               width == 32U;
    };
    if (!valid_width(source_width) || source_width > 8U || !valid_width(destination_width)) {
        return;
    }

    std::uint32_t output = 0;
    std::uint32_t output_bits = 0;
    const auto source_mask = (1U << source_width) - 1U;
    for (std::uint32_t index = 0; index < source_length; ++index) {
        const auto byte = memory.read8(source++);
        for (std::uint32_t shift = 0; shift < 8U; shift += source_width) {
            auto value = (static_cast<std::uint32_t>(byte) >> shift) & source_mask;
            if (value != 0U || offset_zero_values) {
                value += data_offset;
            }
            if (destination_width < 32U) {
                value &= (1U << destination_width) - 1U;
            }
            output |= value << output_bits;
            output_bits += destination_width;
            if (output_bits >= 32U) {
                memory.write32(destination, output);
                destination += 4U;
                output = 0;
                output_bits = 0;
            }
        }
    }
}

[[nodiscard]] std::int32_t arctan(const std::int32_t tangent) noexcept {
    const auto a = -((tangent * tangent) >> 14);
    auto b = ((0xA9 * a) >> 14) + 0x390;
    b = ((b * a) >> 14) + 0x91C;
    b = ((b * a) >> 14) + 0xFB6;
    b = ((b * a) >> 14) + 0x16AA;
    b = ((b * a) >> 14) + 0x2081;
    b = ((b * a) >> 14) + 0x3651;
    b = ((b * a) >> 14) + 0xA2F9;
    return (tangent * b) >> 16;
}

[[nodiscard]] std::uint32_t arctan2(const std::int32_t x, const std::int32_t y) noexcept {
    if (y == 0) {
        return x >= 0 ? 0U : 0x8000U;
    }
    if (x == 0) {
        return y >= 0 ? 0x4000U : 0xC000U;
    }
    const auto ratio = [](const std::int32_t numerator, const std::int32_t denominator) {
        return static_cast<std::int32_t>((static_cast<std::int64_t>(numerator) << 14) /
                                         denominator);
    };
    std::int32_t result = 0;
    if (y >= 0) {
        if (x >= 0) {
            result = x >= y ? arctan(ratio(y, x)) : 0x4000 - arctan(ratio(x, y));
        } else {
            result = -x >= y ? arctan(ratio(y, x)) + 0x8000 : 0x4000 - arctan(ratio(x, y));
        }
    } else {
        if (x <= 0) {
            result = -x > -y ? arctan(ratio(y, x)) + 0x8000 : 0xC000 - arctan(ratio(x, y));
        } else {
            result = x >= -y ? arctan(ratio(y, x)) + 0x10000 : 0xC000 - arctan(ratio(x, y));
        }
    }
    return static_cast<std::uint32_t>(result) & 0xFFFFU;
}

[[nodiscard]] double angle_radians(const std::uint16_t angle) noexcept {
    return static_cast<double>(angle >> 8U) / 128.0 * std::numbers::pi;
}

void bg_affine_set(Memory& memory, std::uint32_t source, std::uint32_t destination,
                   const std::uint32_t count) noexcept {
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto origin_x =
            static_cast<double>(static_cast<std::int32_t>(memory.read32(source))) / 256.0;
        const auto origin_y =
            static_cast<double>(static_cast<std::int32_t>(memory.read32(source + 4U))) / 256.0;
        const auto center_x =
            static_cast<double>(static_cast<std::int16_t>(memory.read16(source + 8U)));
        const auto center_y =
            static_cast<double>(static_cast<std::int16_t>(memory.read16(source + 10U)));
        const auto scale_x =
            static_cast<double>(static_cast<std::int16_t>(memory.read16(source + 12U))) / 256.0;
        const auto scale_y =
            static_cast<double>(static_cast<std::int16_t>(memory.read16(source + 14U))) / 256.0;
        const auto theta = angle_radians(memory.read16(source + 16U));
        source += 20U;

        const auto pa = std::cos(theta) * scale_x;
        const auto pb = -std::sin(theta) * scale_x;
        const auto pc = std::sin(theta) * scale_y;
        const auto pd = std::cos(theta) * scale_y;
        const auto reference_x = origin_x - (pa * center_x + pb * center_y);
        const auto reference_y = origin_y - (pc * center_x + pd * center_y);

        const auto fixed16 = [](const double value) {
            return static_cast<std::uint16_t>(static_cast<std::int32_t>(value * 256.0));
        };
        const auto fixed32 = [](const double value) {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(value * 256.0));
        };
        memory.write16(destination, fixed16(pa));
        memory.write16(destination + 2U, fixed16(pb));
        memory.write16(destination + 4U, fixed16(pc));
        memory.write16(destination + 6U, fixed16(pd));
        memory.write32(destination + 8U, fixed32(reference_x));
        memory.write32(destination + 12U, fixed32(reference_y));
        destination += 16U;
    }
}

void obj_affine_set(Memory& memory, std::uint32_t source, std::uint32_t destination,
                    const std::uint32_t count, const std::uint32_t stride) noexcept {
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto scale_x =
            static_cast<double>(static_cast<std::int16_t>(memory.read16(source))) / 256.0;
        const auto scale_y =
            static_cast<double>(static_cast<std::int16_t>(memory.read16(source + 2U))) / 256.0;
        const auto theta = angle_radians(memory.read16(source + 4U));
        source += 8U;

        const auto fixed16 = [](const double value) {
            return static_cast<std::uint16_t>(static_cast<std::int32_t>(value * 256.0));
        };
        memory.write16(destination, fixed16(std::cos(theta) * scale_x));
        memory.write16(destination + stride, fixed16(-std::sin(theta) * scale_x));
        memory.write16(destination + stride * 2U, fixed16(std::sin(theta) * scale_y));
        memory.write16(destination + stride * 3U, fixed16(std::cos(theta) * scale_y));
        destination += stride * 4U;
    }
}

[[nodiscard]] std::uint32_t cpu_set(Memory& memory, std::uint32_t source, std::uint32_t destination,
                                    const std::uint32_t control) {
    // The BIOS refuses to copy from its own address space.
    if ((source & 0x0E000000U) == 0U) {
        return 0;
    }
    const auto count = control & 0x1FFFFFU;
    const bool fill = (control & (1U << 24U)) != 0U;
    const bool words = (control & (1U << 26U)) != 0U;
    if (words) {
        source &= ~3U;
        destination &= ~3U;
        const auto value = memory.read32(source);
        for (std::uint32_t index = 0; index < count; ++index) {
            memory.write32(destination + index * 4U,
                           fill ? value : memory.read32(source + index * 4U));
        }
    } else {
        source &= ~1U;
        destination &= ~1U;
        const auto value = memory.read16(source);
        for (std::uint32_t index = 0; index < count; ++index) {
            memory.write16(destination + index * 2U,
                           fill ? value : memory.read16(source + index * 2U));
        }
    }
    return count * 2U;
}

[[nodiscard]] std::uint32_t cpu_fast_set(Memory& memory, std::uint32_t source,
                                         std::uint32_t destination, const std::uint32_t control) {
    if ((source & 0x0E000000U) == 0U) {
        return 0;
    }
    const auto count = ((control & 0x1FFFFFU) + 7U) & ~7U;
    const bool fill = (control & (1U << 24U)) != 0U;
    source &= ~3U;
    destination &= ~3U;
    const auto value = memory.read32(source);
    for (std::uint32_t index = 0; index < count; ++index) {
        memory.write32(destination + index * 4U, fill ? value : memory.read32(source + index * 4U));
    }
    return count;
}

void register_ram_reset(Memory& memory, const std::uint32_t flags) noexcept {
    const auto clear = [&memory](const std::uint32_t start, const std::uint32_t size) {
        for (std::uint32_t offset = 0; offset < size; offset += 4U) {
            memory.write32(start + offset, 0);
        }
    };
    if ((flags & 0x01U) != 0U) {
        clear(GbaBus::kEwramStart, static_cast<std::uint32_t>(GbaBus::kEwramSize));
    }
    if ((flags & 0x02U) != 0U) {
        // The top 0x200 bytes hold the BIOS stacks and interrupt vector.
        clear(GbaBus::kIwramStart, static_cast<std::uint32_t>(GbaBus::kIwramSize) - 0x200U);
    }
    if ((flags & 0x04U) != 0U) {
        clear(GbaBus::kPaletteStart, static_cast<std::uint32_t>(GbaBus::kPaletteSize));
    }
    if ((flags & 0x08U) != 0U) {
        clear(GbaBus::kVramStart, static_cast<std::uint32_t>(GbaBus::kVramSize));
    }
    if ((flags & 0x10U) != 0U) {
        clear(GbaBus::kOamStart, static_cast<std::uint32_t>(GbaBus::kOamSize));
    }
    if ((flags & 0x80U) != 0U) {
        clear(GbaBus::kIoStart, 0x60U);          // display registers
        clear(GbaBus::kIoStart + 0xB0U, 0x30U);  // DMA
        clear(GbaBus::kIoStart + 0x100U, 0x10U); // timers
        memory.write16(GbaBus::kIoStart, 0x0080U);
        memory.write16(GbaBus::kIoStart + 0x20U, 0x0100U);
        memory.write16(GbaBus::kIoStart + 0x26U, 0x0100U);
        memory.write16(GbaBus::kIoStart + 0x30U, 0x0100U);
        memory.write16(GbaBus::kIoStart + 0x36U, 0x0100U);
    }
}

void soft_reset(Arm7Tdmi& cpu, Memory& memory) noexcept {
    const bool boot_from_ewram = memory.read8(0x03007FFAU) != 0U;
    for (std::uint32_t address = 0x03007E00U; address < 0x03008000U; address += 4U) {
        memory.write32(address, 0);
    }
    cpu.cpsr().set_mode(ProcessorMode::Supervisor);
    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007FE0U);
    cpu.set_register(Arm7Tdmi::kLinkRegister, 0);
    static_cast<void>(cpu.spsr(ProcessorMode::Supervisor)->assign(0x1FU));
    cpu.cpsr().set_mode(ProcessorMode::Irq);
    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007FA0U);
    cpu.set_register(Arm7Tdmi::kLinkRegister, 0);
    static_cast<void>(cpu.spsr(ProcessorMode::Irq)->assign(0x1FU));
    static_cast<void>(cpu.cpsr().assign(0x1FU)); // System mode, ARM, interrupts enabled
    cpu.set_register(Arm7Tdmi::kStackPointer, 0x03007F00U);
    for (std::size_t index = 0; index < 13U; ++index) {
        cpu.set_register(index, 0);
    }
    const auto entry = boot_from_ewram ? GbaBus::kEwramStart : GbaBus::kGamePakStart;
    cpu.set_register(Arm7Tdmi::kLinkRegister, entry);
    cpu.set_program_counter(entry);
}

} // namespace

std::uint8_t HleBios::pending_swi_number(const Arm7Tdmi& cpu, GbaBus& bus) noexcept {
    const auto return_address = cpu.register_value(Arm7Tdmi::kLinkRegister);
    const auto* saved = cpu.current_spsr();
    const bool thumb = saved != nullptr && saved->instruction_set() == InstructionSet::Thumb;
    if (thumb) {
        return static_cast<std::uint8_t>(bus.read16(return_address - 2U, kBiosAccess).value);
    }
    return static_cast<std::uint8_t>(bus.read32(return_address - 4U, kBiosAccess).value >> 16U);
}

std::uint32_t HleBios::handle_swi(Arm7Tdmi& cpu, GbaBus& bus) noexcept {
    Memory memory(bus);
    const auto number = pending_swi_number(cpu, bus);
    const auto return_address = cpu.register_value(Arm7Tdmi::kLinkRegister);
    const auto r0 = cpu.register_value(0);
    const auto r1 = cpu.register_value(1);
    const auto r2 = cpu.register_value(2);
    const auto r3 = cpu.register_value(3);
    std::uint32_t cycles = 24;

    const auto finish = [&]() {
        cpu.return_from_exception(return_address);
        bus.set_bios_latch(builtin_bios::kSwiReturnLatch);
        return cycles;
    };

    switch (number) {
    case 0x00: // SoftReset
        soft_reset(cpu, memory);
        bus.set_bios_latch(builtin_bios::kSwiReturnLatch);
        return cycles;
    case 0x01: // RegisterRamReset
        register_ram_reset(memory, r0);
        cycles += 1000;
        break;
    case 0x02: // Halt
        memory.write8(GbaBus::kIoStart + 0x301U, 0x00U);
        break;
    case 0x03: // Stop
        memory.write8(GbaBus::kIoStart + 0x301U, 0x80U);
        break;
    case 0x05: // VBlankIntrWait
        cpu.set_register(0, 1);
        cpu.set_register(1, 1);
        [[fallthrough]];
    case 0x04: // IntrWait: run the wait loop in the built-in system ROM.
        cpu.set_program_counter(builtin_bios::kIntrWaitRoutine);
        return cycles;
    case 0x06:   // Div
    case 0x07: { // DivArm
        const auto numerator = static_cast<std::int32_t>(number == 0x06 ? r0 : r1);
        const auto denominator = static_cast<std::int32_t>(number == 0x06 ? r1 : r0);
        std::int32_t quotient = 0;
        std::int32_t remainder = 0;
        if (denominator == 0) {
            // Hardware loops forever; returning a saturated result keeps the emulator usable.
            quotient = numerator < 0 ? -1 : 1;
            remainder = numerator;
        } else if (numerator == std::numeric_limits<std::int32_t>::min() && denominator == -1) {
            quotient = numerator;
            remainder = 0;
        } else {
            quotient = numerator / denominator;
            remainder = numerator % denominator;
        }
        cpu.set_register(0, static_cast<std::uint32_t>(quotient));
        cpu.set_register(1, static_cast<std::uint32_t>(remainder));
        cpu.set_register(3,
                         static_cast<std::uint32_t>(std::abs(static_cast<std::int64_t>(quotient))));
        cycles += 40;
        break;
    }
    case 0x08: { // Sqrt
        auto root = static_cast<std::uint32_t>(std::sqrt(static_cast<double>(r0)));
        while (static_cast<std::uint64_t>(root) * root > r0) {
            --root;
        }
        while (static_cast<std::uint64_t>(root + 1U) * (root + 1U) <= r0) {
            ++root;
        }
        cpu.set_register(0, root);
        cycles += 40;
        break;
    }
    case 0x09: // ArcTan
        cpu.set_register(0, static_cast<std::uint32_t>(arctan(static_cast<std::int16_t>(r0))));
        break;
    case 0x0A: // ArcTan2
        cpu.set_register(0, arctan2(static_cast<std::int16_t>(r0), static_cast<std::int16_t>(r1)));
        break;
    case 0x0B: // CpuSet
        cycles += cpu_set(memory, r0, r1, r2);
        break;
    case 0x0C: // CpuFastSet
        cycles += cpu_fast_set(memory, r0, r1, r2);
        break;
    case 0x0D: // GetBiosChecksum
        cpu.set_register(0, kBiosChecksum);
        break;
    case 0x0E: // BgAffineSet
        bg_affine_set(memory, r0, r1, r2);
        break;
    case 0x0F: // ObjAffineSet
        obj_affine_set(memory, r0, r1, r2, r3);
        break;
    case 0x10: // BitUnPack
        bit_unpack(memory, r0, r1, r2);
        break;
    case 0x11: // LZ77UnCompReadNormalWrite8bit
        cycles += lz77_uncompress(memory, r0, r1, Destination::Wram);
        break;
    case 0x12: // LZ77UnCompReadNormalWrite16bit
        cycles += lz77_uncompress(memory, r0, r1, Destination::Vram);
        break;
    case 0x13: // HuffUnCompReadNormal
        cycles += huffman_uncompress(memory, r0, r1);
        break;
    case 0x14: // RLUnCompReadNormalWrite8bit
        cycles += rl_uncompress(memory, r0, r1, Destination::Wram);
        break;
    case 0x15: // RLUnCompReadNormalWrite16bit
        cycles += rl_uncompress(memory, r0, r1, Destination::Vram);
        break;
    case 0x16: // Diff8bitUnFilterWrite8bit
        diff_unfilter(memory, r0, r1, false, Destination::Wram);
        break;
    case 0x17: // Diff8bitUnFilterWrite16bit
        diff_unfilter(memory, r0, r1, false, Destination::Vram);
        break;
    case 0x18: // Diff16bitUnFilter
        diff_unfilter(memory, r0, r1, true, Destination::Vram);
        break;
    default:
        // Sound driver, multiboot, and other services arrive with the subsystems they drive.
        break;
    }
    return finish();
}

} // namespace srgba::core
