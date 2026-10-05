#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace srgba::core {

class GbaBus;

enum class DmaTiming : std::uint8_t {
    Immediate = 0,
    VBlank = 1,
    HBlank = 2,
    Special = 3, // sound FIFO (channels 1-2) or video capture (channel 3)
};

// The four DMA channels (DMA0CNT-DMA3CNT). Transfers run to completion when triggered; the CPU
// is stalled for the cycles they take, which the emulator collects with take_stall_cycles().
class DmaController {
  public:
    static constexpr std::size_t kChannelCount = 4;

    void reset() noexcept;

    // `offset` is relative to 0x040000B0 (0x00-0x2F).
    [[nodiscard]] std::uint8_t read(std::uint32_t offset) const noexcept;
    void write(std::uint32_t offset, std::uint8_t value, GbaBus& bus) noexcept;

    // Starts every enabled channel waiting for `timing`, in priority order (DMA0 first).
    void trigger(DmaTiming timing, GbaBus& bus) noexcept;
    // Starts one channel if it is enabled and waiting for `timing` (used by the sound FIFOs).
    void trigger_channel(std::size_t channel, DmaTiming timing, GbaBus& bus) noexcept;

    [[nodiscard]] std::uint32_t take_stall_cycles() noexcept;
    [[nodiscard]] bool enabled(std::size_t channel) const noexcept;
    [[nodiscard]] DmaTiming timing(std::size_t channel) const noexcept;

  private:
    struct Channel {
        std::uint32_t source{};
        std::uint32_t destination{};
        std::uint16_t count{};
        std::uint16_t control{};
        std::uint32_t internal_source{};
        std::uint32_t internal_destination{};
        std::uint32_t internal_count{};
    };

    void latch(std::size_t channel) noexcept;
    void run(std::size_t channel, GbaBus& bus) noexcept;

    std::array<Channel, kChannelCount> channels_{};
    std::uint32_t stall_cycles_{};
    std::uint32_t last_value_{};
};

} // namespace srgba::core
