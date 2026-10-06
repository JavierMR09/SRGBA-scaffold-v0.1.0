#include "srgba/core/timers.hpp"
#include "srgba/core/state_io.hpp"

#include "srgba/core/gba_bus.hpp"
#include "srgba/core/interrupts.hpp"
#include "srgba/core/scheduler.hpp"

namespace srgba::core {
namespace {

constexpr std::array<unsigned, 4> kPrescalerShifts{0U, 6U, 8U, 10U};
constexpr std::uint16_t kCountUp = 1U << 2U;
constexpr std::uint16_t kIrqEnable = 1U << 6U;
constexpr std::uint16_t kStart = 1U << 7U;
constexpr std::uint16_t kControlMask = 0x00C7U;
constexpr std::uint32_t kOverflow = 0x10000U;

[[nodiscard]] EventType event_for(const std::size_t index) noexcept {
    return static_cast<EventType>(static_cast<std::size_t>(EventType::Timer0Overflow) + index);
}

} // namespace

void Timers::reset() noexcept {
    timers_.fill(Timer{});
}

bool Timers::counts_up(const std::size_t index) const noexcept {
    return index > 0U && (timers_[index].control & kCountUp) != 0U;
}

unsigned Timers::prescaler_shift(const std::size_t index) const noexcept {
    return kPrescalerShifts[timers_[index].control & 0x3U];
}

std::uint16_t Timers::counter(const std::size_t index, const std::uint64_t now) const noexcept {
    const auto& timer = timers_[index];
    if (!timer.running || counts_up(index) || now <= timer.start_time) {
        return timer.counter;
    }
    const auto ticks = (now - timer.start_time) >> prescaler_shift(index);
    return static_cast<std::uint16_t>((timer.counter + ticks) & 0xFFFFU);
}

std::uint16_t Timers::reload(const std::size_t index) const noexcept {
    return timers_[index].reload;
}

std::uint16_t Timers::control(const std::size_t index) const noexcept {
    return timers_[index].control;
}

bool Timers::running(const std::size_t index) const noexcept {
    return timers_[index].running;
}

std::uint8_t Timers::read(const std::uint32_t offset, const std::uint64_t now) const noexcept {
    const auto index = static_cast<std::size_t>((offset >> 2U) & 0x3U);
    switch (offset & 0x3U) {
    case 0:
        return static_cast<std::uint8_t>(counter(index, now));
    case 1:
        return static_cast<std::uint8_t>(counter(index, now) >> 8U);
    case 2:
        return static_cast<std::uint8_t>(timers_[index].control);
    default:
        return 0;
    }
}

void Timers::synchronize(const std::size_t index, const std::uint64_t now) noexcept {
    auto& timer = timers_[index];
    if (!timer.running || counts_up(index) || now <= timer.start_time) {
        return;
    }
    const auto shift = prescaler_shift(index);
    const auto ticks = (now - timer.start_time) >> shift;
    timer.counter = static_cast<std::uint16_t>((timer.counter + ticks) & 0xFFFFU);
    // Keep the prescaler phase: only whole ticks are consumed.
    timer.start_time += ticks << shift;
}

void Timers::schedule(const std::size_t index, Scheduler& scheduler) noexcept {
    const auto& timer = timers_[index];
    if (!timer.running || counts_up(index)) {
        scheduler.cancel(event_for(index));
        return;
    }
    const auto remaining = static_cast<std::uint64_t>(kOverflow - timer.counter);
    scheduler.schedule_at(event_for(index),
                          timer.start_time + (remaining << prescaler_shift(index)));
}

void Timers::write(const std::uint32_t offset, const std::uint8_t value,
                   Scheduler& scheduler) noexcept {
    const auto index = static_cast<std::size_t>((offset >> 2U) & 0x3U);
    auto& timer = timers_[index];
    switch (offset & 0x3U) {
    case 0:
        timer.reload = static_cast<std::uint16_t>((timer.reload & 0xFF00U) | value);
        return;
    case 1:
        timer.reload = static_cast<std::uint16_t>((timer.reload & 0x00FFU) |
                                                  (static_cast<unsigned>(value) << 8U));
        return;
    case 2: {
        const auto now = scheduler.now();
        synchronize(index, now);
        const bool was_running = timer.running;
        const bool was_counting_up = counts_up(index);
        timer.control = static_cast<std::uint16_t>(value & kControlMask);
        const bool start = (timer.control & kStart) != 0U;
        if (start && !was_running) {
            timer.counter = timer.reload;
            timer.running = true;
            timer.start_time = now;
        } else if (!start) {
            timer.running = false;
        } else if (was_counting_up && !counts_up(index)) {
            // Switching from cascade to the prescaler starts counting from now.
            timer.start_time = now;
        }
        schedule(index, scheduler);
        return;
    }
    default:
        return;
    }
}

std::uint8_t Timers::on_overflow(const std::size_t index, const std::uint64_t timestamp,
                                 Scheduler& scheduler, GbaBus& bus) noexcept {
    if (!timers_[index].running || counts_up(index)) {
        return 0;
    }
    return overflow(index, timestamp, scheduler, bus);
}

std::uint8_t Timers::overflow(const std::size_t index, const std::uint64_t timestamp,
                              Scheduler& scheduler, GbaBus& bus) noexcept {
    auto& timer = timers_[index];
    timer.counter = timer.reload;
    timer.start_time = timestamp;
    auto overflowed = static_cast<std::uint8_t>(1U << index);
    if ((timer.control & kIrqEnable) != 0U) {
        bus.request_interrupts(
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(Interrupt::Timer0) << index));
    }

    const auto next = index + 1U;
    if (next < kTimerCount && timers_[next].running && counts_up(next)) {
        auto& cascade = timers_[next];
        if (cascade.counter == 0xFFFFU) {
            overflowed =
                static_cast<std::uint8_t>(overflowed | overflow(next, timestamp, scheduler, bus));
        } else {
            ++cascade.counter;
        }
    }
    schedule(index, scheduler);
    return overflowed;
}

void Timers::save_state(StateWriter& writer) const {
    writer.section("TMRS");
    for (const auto& timer : timers_) {
        writer.u16(timer.reload);
        writer.u16(timer.control);
        writer.u16(timer.counter);
        writer.u64(timer.start_time);
        writer.boolean(timer.running);
    }
}

void Timers::load_state(StateReader& reader) {
    reader.section("TMRS");
    for (auto& timer : timers_) {
        timer.reload = reader.u16();
        timer.control = reader.u16();
        timer.counter = reader.u16();
        timer.start_time = reader.u64();
        timer.running = reader.boolean();
    }
}

} // namespace srgba::core
