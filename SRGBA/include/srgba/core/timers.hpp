#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace srgba::core {

class GbaBus;
class Scheduler;

// The four 16-bit hardware timers (TM0CNT-TM3CNT). Free-running timers are evaluated lazily from
// the master clock and only touch the scheduler to arm their next overflow; count-up (cascade)
// timers advance when the previous timer overflows.
class Timers {
  public:
    static constexpr std::size_t kTimerCount = 4;

    void reset() noexcept;

    // `offset` is relative to 0x04000100 (0x00-0x0F).
    [[nodiscard]] std::uint8_t read(std::uint32_t offset, std::uint64_t now) const noexcept;
    void write(std::uint32_t offset, std::uint8_t value, Scheduler& scheduler) noexcept;

    // Handles the scheduled overflow of timer `index` at `timestamp`. Returns a mask of every
    // timer that overflowed (cascades included), which the audio FIFOs use from M5 onwards.
    std::uint8_t on_overflow(std::size_t index, std::uint64_t timestamp, Scheduler& scheduler,
                             GbaBus& bus) noexcept;

    [[nodiscard]] std::uint16_t counter(std::size_t index, std::uint64_t now) const noexcept;
    [[nodiscard]] std::uint16_t reload(std::size_t index) const noexcept;
    [[nodiscard]] std::uint16_t control(std::size_t index) const noexcept;
    [[nodiscard]] bool running(std::size_t index) const noexcept;

  private:
    struct Timer {
        std::uint16_t reload{};
        std::uint16_t control{};
        // Counter value at `start_time` (free-running) or the live value (count-up).
        std::uint16_t counter{};
        std::uint64_t start_time{};
        bool running{};
    };

    [[nodiscard]] bool counts_up(std::size_t index) const noexcept;
    [[nodiscard]] unsigned prescaler_shift(std::size_t index) const noexcept;
    void synchronize(std::size_t index, std::uint64_t now) noexcept;
    void schedule(std::size_t index, Scheduler& scheduler) noexcept;
    std::uint8_t overflow(std::size_t index, std::uint64_t timestamp, Scheduler& scheduler,
                          GbaBus& bus) noexcept;

    std::array<Timer, kTimerCount> timers_{};
};

} // namespace srgba::core
