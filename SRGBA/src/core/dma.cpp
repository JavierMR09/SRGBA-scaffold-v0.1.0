#include "srgba/core/dma.hpp"

#include "srgba/core/gba_bus.hpp"
#include "srgba/core/interrupts.hpp"

namespace srgba::core {
namespace {

constexpr std::uint16_t kEnable = 1U << 15U;
constexpr std::uint16_t kIrqEnable = 1U << 14U;
constexpr std::uint16_t kWordTransfer = 1U << 10U;
constexpr std::uint16_t kRepeat = 1U << 9U;

constexpr std::array<std::uint32_t, 4> kSourceMasks{0x07FFFFFFU, 0x0FFFFFFFU, 0x0FFFFFFFU,
                                                    0x0FFFFFFFU};
constexpr std::array<std::uint32_t, 4> kDestinationMasks{0x07FFFFFFU, 0x07FFFFFFU, 0x07FFFFFFU,
                                                         0x0FFFFFFFU};
constexpr std::array<std::uint16_t, 4> kControlMasks{0xF7E0U, 0xF7E0U, 0xF7E0U, 0xFFE0U};

enum class AddressControl : std::uint8_t {
    Increment = 0,
    Decrement = 1,
    Fixed = 2,
    IncrementReload = 3,
};

[[nodiscard]] std::uint32_t max_count(const std::size_t channel) noexcept {
    return channel == 3U ? 0x10000U : 0x4000U;
}

[[nodiscard]] bool in_game_pak(const std::uint32_t address) noexcept {
    return address >= 0x08000000U && address < 0x0E000000U;
}

[[nodiscard]] std::uint32_t step(const AddressControl control, const std::uint32_t width) noexcept {
    switch (control) {
    case AddressControl::Increment:
    case AddressControl::IncrementReload:
        return width;
    case AddressControl::Decrement:
        return 0U - width;
    case AddressControl::Fixed:
        return 0;
    }
    return width;
}

} // namespace

void DmaController::reset() noexcept {
    channels_.fill(Channel{});
    stall_cycles_ = 0;
    last_value_ = 0;
}

std::uint8_t DmaController::read(const std::uint32_t offset) const noexcept {
    const auto channel = static_cast<std::size_t>(offset / 12U);
    const auto field = offset % 12U;
    if (channel >= kChannelCount) {
        return 0;
    }
    // Only the control register is readable; addresses and counts are write-only.
    if (field == 10U) {
        return static_cast<std::uint8_t>(channels_[channel].control);
    }
    if (field == 11U) {
        return static_cast<std::uint8_t>(channels_[channel].control >> 8U);
    }
    return 0;
}

void DmaController::write(const std::uint32_t offset, const std::uint8_t value,
                          GbaBus& bus) noexcept {
    const auto index = static_cast<std::size_t>(offset / 12U);
    const auto field = offset % 12U;
    if (index >= kChannelCount) {
        return;
    }
    auto& channel = channels_[index];
    const auto shift = static_cast<unsigned>((field & 3U) * 8U);
    if (field < 4U) {
        channel.source =
            (channel.source & ~(0xFFU << shift)) | (static_cast<std::uint32_t>(value) << shift);
        return;
    }
    if (field < 8U) {
        channel.destination = (channel.destination & ~(0xFFU << shift)) |
                              (static_cast<std::uint32_t>(value) << shift);
        return;
    }
    if (field < 10U) {
        const auto count_shift = static_cast<unsigned>((field - 8U) * 8U);
        channel.count = static_cast<std::uint16_t>((channel.count & ~(0xFFU << count_shift)) |
                                                   (static_cast<unsigned>(value) << count_shift));
        return;
    }

    const auto control_shift = static_cast<unsigned>((field - 10U) * 8U);
    const bool was_enabled = (channel.control & kEnable) != 0U;
    channel.control = static_cast<std::uint16_t>(((channel.control & ~(0xFFU << control_shift)) |
                                                  (static_cast<unsigned>(value) << control_shift)) &
                                                 kControlMasks[index]);
    const bool enabled = (channel.control & kEnable) != 0U;
    if (!was_enabled && enabled) {
        latch(index);
        if (timing(index) == DmaTiming::Immediate) {
            run(index, bus);
        }
    }
}

void DmaController::latch(const std::size_t index) noexcept {
    auto& channel = channels_[index];
    channel.internal_source = channel.source & kSourceMasks[index];
    channel.internal_destination = channel.destination & kDestinationMasks[index];
    const auto count = channel.count & (max_count(index) - 1U);
    channel.internal_count = count == 0U ? max_count(index) : count;
}

void DmaController::trigger(const DmaTiming timing_value, GbaBus& bus) noexcept {
    for (std::size_t index = 0; index < kChannelCount; ++index) {
        trigger_channel(index, timing_value, bus);
    }
}

void DmaController::trigger_channel(const std::size_t index, const DmaTiming timing_value,
                                    GbaBus& bus) noexcept {
    if (enabled(index) && timing(index) == timing_value) {
        run(index, bus);
    }
}

void DmaController::request_sound_fifo(const std::uint32_t fifo_address, GbaBus& bus) noexcept {
    for (std::size_t index = 1; index <= 2U; ++index) {
        if (enabled(index) && timing(index) == DmaTiming::Special &&
            channels_[index].internal_destination == fifo_address) {
            run(index, bus);
            return;
        }
    }
}

std::uint32_t DmaController::take_stall_cycles() noexcept {
    const auto cycles = stall_cycles_;
    stall_cycles_ = 0;
    return cycles;
}

bool DmaController::enabled(const std::size_t channel) const noexcept {
    return (channels_[channel].control & kEnable) != 0U;
}

DmaTiming DmaController::timing(const std::size_t channel) const noexcept {
    return static_cast<DmaTiming>((channels_[channel].control >> 12U) & 0x3U);
}

void DmaController::run(const std::size_t index, GbaBus& bus) noexcept {
    auto& channel = channels_[index];
    // Sound FIFO mode always moves four words to a fixed address, ignoring count and width.
    const bool sound_fifo = timing(index) == DmaTiming::Special && (index == 1U || index == 2U);
    const bool words = sound_fifo || (channel.control & kWordTransfer) != 0U;
    const std::uint32_t width = words ? 4U : 2U;
    const auto destination_control =
        sound_fifo ? AddressControl::Fixed
                   : static_cast<AddressControl>((channel.control >> 5U) & 0x3U);
    const auto units = sound_fifo ? 4U : channel.internal_count;
    auto source_control = static_cast<AddressControl>((channel.control >> 7U) & 0x3U);
    if (source_control == AddressControl::IncrementReload || in_game_pak(channel.internal_source)) {
        // Mode 3 is prohibited for sources, and the cartridge bus can only count upwards.
        source_control = AddressControl::Increment;
    }
    const auto source_step = step(source_control, width);
    const auto destination_step = step(destination_control, width);

    // EEPROM commands are sent by DMA; their length reveals the chip's address width.
    if (bus.is_eeprom_address(channel.internal_destination)) {
        bus.backup().prepare_eeprom_transfer(channel.internal_count);
    }

    auto source = channel.internal_source;
    auto destination = channel.internal_destination;
    std::uint32_t cycles = 2; // DMA start-up
    for (std::uint32_t unit = 0; unit < units; ++unit) {
        const BusAccess access{unit == 0U ? AccessSequence::NonSequential
                                          : AccessSequence::Sequential,
                               AccessKind::Data, 0xFFFFFFFFU};
        // DMA cannot read the BIOS; it sees the last value it transferred instead.
        const bool readable = source >= 0x02000000U;
        if (words) {
            if (readable) {
                const auto read = bus.read32(source & ~3U, access);
                last_value_ = read.value;
                cycles += read.cycles;
            } else {
                cycles += 1U;
            }
            cycles += bus.write32(destination & ~3U, last_value_, access).cycles;
        } else {
            if (readable) {
                const auto read = bus.read16(source & ~1U, access);
                last_value_ = (read.value & 0xFFFFU) * 0x00010001U;
                cycles += read.cycles;
            } else {
                cycles += 1U;
            }
            cycles +=
                bus.write16(destination & ~1U, static_cast<std::uint16_t>(last_value_), access)
                    .cycles;
        }
        source += source_step;
        destination += destination_step;
    }

    channel.internal_source = source;
    channel.internal_destination = destination;
    stall_cycles_ += cycles;

    if ((channel.control & kRepeat) != 0U && timing(index) != DmaTiming::Immediate) {
        const auto count = channel.count & (max_count(index) - 1U);
        channel.internal_count = count == 0U ? max_count(index) : count;
        if (destination_control == AddressControl::IncrementReload) {
            channel.internal_destination = channel.destination & kDestinationMasks[index];
        }
    } else {
        channel.control = static_cast<std::uint16_t>(channel.control & ~kEnable);
    }

    if ((channel.control & kIrqEnable) != 0U) {
        bus.request_interrupts(
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(Interrupt::Dma0) << index));
    }
}

} // namespace srgba::core
