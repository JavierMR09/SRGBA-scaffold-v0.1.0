#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace srgba::core {

// Audio processing unit: the four Game Boy-compatible PSG channels and the two Direct Sound
// FIFOs, mixed into 16-bit stereo at 32,768 Hz (the GBA's default PWM sample rate).
class Apu {
  public:
    static constexpr std::uint32_t kSampleRate = 32768;
    static constexpr std::uint32_t kCyclesPerSample = 512;          // 2^24 / 32768
    static constexpr std::uint32_t kCyclesPerSequencerStep = 32768; // 512 Hz frame sequencer
    // Output is dropped rather than buffered without bound when nobody drains it.
    static constexpr std::size_t kMaximumBufferedFrames = kSampleRate;

    // Bits returned by on_timer_overflow().
    static constexpr std::uint8_t kFifoA = 1U << 0U;
    static constexpr std::uint8_t kFifoB = 1U << 1U;

    void reset() noexcept;

    // `offset` is relative to 0x04000060 (0x00-0x4F).
    [[nodiscard]] std::uint8_t read(std::uint32_t offset) const noexcept;
    void write(std::uint32_t offset, std::uint8_t value) noexcept;

    // Clocks the Direct Sound FIFOs driven by the overflowing timers. Returns the FIFOs that are
    // at most half full and need a DMA refill.
    [[nodiscard]] std::uint8_t on_timer_overflow(std::uint8_t timer_mask,
                                                 std::uint64_t timestamp) noexcept;
    // Produces one stereo output sample covering the 512 cycles ending at `timestamp`.
    void on_sample(std::uint64_t timestamp) noexcept;
    // Advances the 512 Hz frame sequencer (length, sweep, and envelope clocks).
    void on_sequencer_step() noexcept;

    // Moves buffered interleaved stereo samples (left, right) into `destination`.
    void take_samples(std::vector<std::int16_t>& destination);
    [[nodiscard]] std::size_t buffered_frames() const noexcept;
    [[nodiscard]] bool channel_active(std::size_t channel) const noexcept;
    [[nodiscard]] std::size_t fifo_size(std::size_t fifo) const noexcept;

  private:
    struct Envelope {
        std::uint8_t initial{};
        std::uint8_t period{};
        bool increase{};
        std::uint8_t volume{};
        std::uint8_t timer{};

        void load(std::uint8_t value) noexcept;
        void trigger() noexcept;
        void clock() noexcept;
    };

    struct Square {
        bool enabled{};
        bool dac{};
        std::uint16_t frequency{};
        std::uint8_t duty{};
        std::uint8_t duty_step{};
        std::int32_t timer{};
        std::uint16_t length{};
        bool length_enable{};
        Envelope envelope{};
        // Sweep (channel 1 only).
        std::uint8_t sweep_period{};
        std::uint8_t sweep_shift{};
        bool sweep_negate{};
        std::uint8_t sweep_timer{};
        std::uint16_t shadow_frequency{};
        bool sweep_enabled{};

        [[nodiscard]] std::int32_t period() const noexcept;
        [[nodiscard]] std::int32_t output() const noexcept;
        [[nodiscard]] std::int64_t advance(std::int32_t cycles) noexcept;
    };

    struct Wave {
        bool enabled{};
        bool dac{};
        std::uint16_t frequency{};
        bool two_banks{};
        std::uint8_t selected_bank{};
        std::uint8_t position{};
        std::int32_t timer{};
        std::uint16_t length{};
        bool length_enable{};
        std::uint8_t volume_code{};
        bool force_75_percent{};

        [[nodiscard]] std::int32_t period() const noexcept;
    };

    struct Noise {
        bool enabled{};
        bool dac{};
        std::uint16_t lfsr{0x7FFFU};
        bool narrow{};
        std::uint8_t divisor{};
        std::uint8_t shift{};
        std::int32_t timer{};
        std::uint16_t length{};
        bool length_enable{};
        Envelope envelope{};

        [[nodiscard]] std::int32_t period() const noexcept;
        [[nodiscard]] std::int32_t output() const noexcept;
        [[nodiscard]] std::int64_t advance(std::int32_t cycles) noexcept;
    };

    struct Fifo {
        std::array<std::int8_t, 32> data{};
        std::size_t read_index{};
        std::size_t size{};
        std::int8_t current{};
        // Output integrated since the last sample, so FIFO rates above 32 kHz are averaged
        // instead of aliased.
        std::int64_t integral{};
        std::uint64_t last_change{};

        void push(std::uint8_t value) noexcept;
        void clear() noexcept;
    };

    [[nodiscard]] std::uint16_t register16(std::uint32_t offset) const noexcept;
    [[nodiscard]] bool master_enabled() const noexcept;
    void write_psg(std::uint32_t offset, std::uint8_t value) noexcept;
    void trigger_square(Square& channel, bool has_sweep) noexcept;
    void trigger_wave() noexcept;
    void trigger_noise() noexcept;
    [[nodiscard]] std::uint16_t sweep_target(Square& channel) noexcept;
    void clock_sweep(Square& channel) noexcept;
    [[nodiscard]] std::int64_t advance_wave(std::int32_t cycles) noexcept;
    [[nodiscard]] std::int32_t wave_output() const noexcept;
    void reset_psg() noexcept;

    std::array<std::uint8_t, 0x50> registers_{};
    std::array<std::array<std::uint8_t, 16>, 2> wave_ram_{};
    Square square1_{};
    Square square2_{};
    Wave wave_{};
    Noise noise_{};
    std::array<Fifo, 2> fifos_{};
    std::uint8_t sequencer_step_{};
    std::uint64_t last_sample_time_{};
    std::vector<std::int16_t> samples_;
};

} // namespace srgba::core
