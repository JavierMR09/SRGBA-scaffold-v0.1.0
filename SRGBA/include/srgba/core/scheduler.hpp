#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace srgba::core {

// Every hardware event that can be raised on the master clock. Later milestones append audio and
// serial events here.
enum class EventType : std::uint8_t {
    HBlankStart,
    ScanlineEnd,
    Timer0Overflow,
    Timer1Overflow,
    Timer2Overflow,
    Timer3Overflow,
    Count,
};

// A deterministic master-cycle scheduler. Each event type has at most one pending deadline, which
// keeps dispatch order stable (ties resolve by event-type order) and makes the state trivially
// serializable for save states.
class Scheduler {
  public:
    static constexpr std::uint64_t kUnscheduled = std::numeric_limits<std::uint64_t>::max();
    static constexpr std::size_t kEventCount = static_cast<std::size_t>(EventType::Count);

    Scheduler() noexcept {
        reset();
    }

    void reset() noexcept {
        now_ = 0;
        deadlines_.fill(kUnscheduled);
        next_deadline_ = kUnscheduled;
    }

    [[nodiscard]] std::uint64_t now() const noexcept {
        return now_;
    }

    void advance(const std::uint64_t cycles) noexcept {
        now_ += cycles;
    }

    // Moves time forward to the next pending event without executing anything. Used while the
    // CPU is halted.
    [[nodiscard]] std::uint64_t skip_to_next_event() noexcept {
        const auto deadline = next_deadline();
        if (deadline == kUnscheduled || deadline <= now_) {
            return 0;
        }
        const auto skipped = deadline - now_;
        now_ = deadline;
        return skipped;
    }

    void schedule(const EventType type, const std::uint64_t delay) noexcept {
        schedule_at(type, now_ + delay);
    }

    void schedule_at(const EventType type, const std::uint64_t timestamp) noexcept {
        deadlines_[index(type)] = timestamp;
        refresh_next_deadline();
    }

    void cancel(const EventType type) noexcept {
        deadlines_[index(type)] = kUnscheduled;
        refresh_next_deadline();
    }

    [[nodiscard]] bool is_scheduled(const EventType type) const noexcept {
        return deadlines_[index(type)] != kUnscheduled;
    }

    [[nodiscard]] std::uint64_t deadline(const EventType type) const noexcept {
        return deadlines_[index(type)];
    }

    [[nodiscard]] std::uint64_t next_deadline() const noexcept {
        return next_deadline_;
    }

    // Removes and returns the earliest event whose deadline has been reached. The returned
    // timestamp lets handlers schedule follow-up events without accumulating drift.
    struct DueEvent {
        EventType type;
        std::uint64_t timestamp;
    };

    [[nodiscard]] std::optional<DueEvent> pop_due() noexcept {
        if (now_ < next_deadline_) {
            return std::nullopt; // fast path: checked after every instruction
        }
        std::size_t best = kEventCount;
        for (std::size_t candidate = 0; candidate < kEventCount; ++candidate) {
            if (deadlines_[candidate] <= now_ &&
                (best == kEventCount || deadlines_[candidate] < deadlines_[best])) {
                best = candidate;
            }
        }
        if (best == kEventCount) {
            return std::nullopt;
        }
        const DueEvent event{static_cast<EventType>(best), deadlines_[best]};
        deadlines_[best] = kUnscheduled;
        refresh_next_deadline();
        return event;
    }

  private:
    void refresh_next_deadline() noexcept {
        next_deadline_ = *std::min_element(deadlines_.begin(), deadlines_.end());
    }

    [[nodiscard]] static constexpr std::size_t index(const EventType type) noexcept {
        return static_cast<std::size_t>(type);
    }

    std::uint64_t now_{};
    std::array<std::uint64_t, kEventCount> deadlines_{};
    std::uint64_t next_deadline_{kUnscheduled};
};

} // namespace srgba::core
