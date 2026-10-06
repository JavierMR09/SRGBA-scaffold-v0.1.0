#include "srgba/core/apu.hpp"

#include <algorithm>

namespace srgba::core {
namespace {

// Register offsets relative to 0x04000060.
constexpr std::uint32_t kSound1Sweep = 0x00U;
constexpr std::uint32_t kSoundControlLow = 0x20U;  // SOUNDCNT_L: PSG volume and routing
constexpr std::uint32_t kSoundControlHigh = 0x22U; // SOUNDCNT_H: mixing and Direct Sound
constexpr std::uint32_t kSoundControlX = 0x24U;    // SOUNDCNT_X: master enable and status
constexpr std::uint32_t kSoundBias = 0x28U;
constexpr std::uint32_t kWaveRamStart = 0x30U;
constexpr std::uint32_t kWaveRamEnd = 0x40U;
constexpr std::uint32_t kFifoStart = 0x40U;
constexpr std::uint32_t kFifoEnd = 0x48U;

// Square wave duty cycles: 12.5%, 25%, 50%, 75%.
constexpr std::array<std::uint8_t, 4> kDutyPatterns{0x01U, 0x81U, 0x87U, 0x7EU};

[[nodiscard]] std::uint16_t read_mask(const std::uint32_t aligned_offset) noexcept {
    switch (aligned_offset) {
    case 0x00U:
        return 0x007FU; // SOUND1CNT_L
    case 0x02U:
    case 0x08U:
        return 0xFFC0U; // duty and envelope (length is write-only)
    case 0x04U:
    case 0x0CU:
    case 0x14U:
        return 0x4000U; // only the length-enable bit reads back
    case 0x10U:
        return 0x00E0U;
    case 0x12U:
        return 0xE000U;
    case 0x18U:
        return 0xFF00U;
    case 0x1CU:
        return 0x40FFU;
    case kSoundControlLow:
        return 0xFF77U;
    case kSoundControlHigh:
        return 0x770FU;
    case kSoundBias:
        return 0xC3FEU;
    default:
        return 0;
    }
}

template <typename Channel> void clock_length(Channel& channel) noexcept {
    if (channel.length_enable && channel.length > 0U) {
        --channel.length;
        if (channel.length == 0U) {
            channel.enabled = false;
        }
    }
}

} // namespace

void Apu::Envelope::load(const std::uint8_t value) noexcept {
    initial = static_cast<std::uint8_t>(value >> 4U);
    increase = (value & 0x08U) != 0U;
    period = static_cast<std::uint8_t>(value & 0x07U);
}

void Apu::Envelope::trigger() noexcept {
    volume = initial;
    timer = period;
}

void Apu::Envelope::clock() noexcept {
    if (period == 0U) {
        return;
    }
    if (timer > 0U) {
        --timer;
    }
    if (timer == 0U) {
        timer = period;
        if (increase && volume < 15U) {
            ++volume;
        } else if (!increase && volume > 0U) {
            --volume;
        }
    }
}

std::int32_t Apu::Square::period() const noexcept {
    return (2048 - static_cast<std::int32_t>(frequency)) * 16;
}

std::int32_t Apu::Square::output() const noexcept {
    if (!enabled) {
        return 0;
    }
    const bool high = ((static_cast<unsigned>(kDutyPatterns[duty]) >> duty_step) & 1U) != 0U;
    return high ? envelope.volume : -static_cast<std::int32_t>(envelope.volume);
}

std::int64_t Apu::Square::advance(std::int32_t cycles) noexcept {
    if (timer <= 0) {
        timer = period();
    }
    // Integrate the output over the interval: a box filter that tames aliasing.
    std::int64_t sum = 0;
    while (cycles > 0) {
        const auto run = std::min(cycles, timer);
        sum += static_cast<std::int64_t>(output()) * run;
        timer -= run;
        cycles -= run;
        if (timer == 0) {
            timer = period();
            duty_step = static_cast<std::uint8_t>((duty_step + 1U) & 7U);
        }
    }
    return sum;
}

std::int32_t Apu::Wave::period() const noexcept {
    return (2048 - static_cast<std::int32_t>(frequency)) * 8;
}

std::int32_t Apu::Noise::period() const noexcept {
    const std::int32_t base = divisor == 0U ? 8 : 16 * static_cast<std::int32_t>(divisor);
    return base << (shift + 2U);
}

std::int32_t Apu::Noise::output() const noexcept {
    if (!enabled) {
        return 0;
    }
    return (lfsr & 1U) == 0U ? envelope.volume : -static_cast<std::int32_t>(envelope.volume);
}

std::int64_t Apu::Noise::advance(std::int32_t cycles) noexcept {
    if (timer <= 0) {
        timer = period();
    }
    std::int64_t sum = 0;
    while (cycles > 0) {
        const auto run = std::min(cycles, timer);
        sum += static_cast<std::int64_t>(output()) * run;
        timer -= run;
        cycles -= run;
        if (timer == 0) {
            timer = period();
            const auto feedback = static_cast<std::uint16_t>((lfsr ^ (lfsr >> 1U)) & 1U);
            lfsr = static_cast<std::uint16_t>((lfsr >> 1U) | (feedback << 14U));
            if (narrow) {
                lfsr = static_cast<std::uint16_t>((lfsr & ~(1U << 6U)) | (feedback << 6U));
            }
        }
    }
    return sum;
}

void Apu::Fifo::push(const std::uint8_t value) noexcept {
    if (size == data.size()) {
        return; // full: the write is lost
    }
    data[(read_index + size) % data.size()] = static_cast<std::int8_t>(value);
    ++size;
}

void Apu::Fifo::clear() noexcept {
    read_index = 0;
    size = 0;
    current = 0;
    integral = 0;
}

void Apu::reset() noexcept {
    registers_.fill(0);
    for (auto& bank : wave_ram_) {
        bank.fill(0);
    }
    square1_ = Square{};
    square2_ = Square{};
    wave_ = Wave{};
    noise_ = Noise{};
    for (auto& fifo : fifos_) {
        fifo.clear();
    }
    sequencer_step_ = 0;
    last_sample_time_ = 0;
    samples_.clear();
}

void Apu::reset_psg() noexcept {
    std::fill_n(registers_.begin(), kSoundControlHigh, std::uint8_t{0});
    square1_ = Square{};
    square2_ = Square{};
    const auto bank = wave_.selected_bank;
    wave_ = Wave{};
    wave_.selected_bank = bank;
    noise_ = Noise{};
}

bool Apu::master_enabled() const noexcept {
    return (registers_[kSoundControlX] & 0x80U) != 0U;
}

std::uint16_t Apu::register16(const std::uint32_t offset) const noexcept {
    return static_cast<std::uint16_t>(registers_[offset] | (registers_[offset + 1U] << 8U));
}

std::uint8_t Apu::read(const std::uint32_t offset) const noexcept {
    if (offset >= kWaveRamStart && offset < kWaveRamEnd) {
        // The CPU sees the bank that is not selected for playback.
        return wave_ram_[wave_.selected_bank ^ 1U][offset - kWaveRamStart];
    }
    const auto aligned = offset & ~1U;
    std::uint16_t value = 0;
    if (aligned == kSoundControlX) {
        value = static_cast<std::uint16_t>(registers_[kSoundControlX] & 0x80U);
        for (std::size_t channel = 0; channel < 4U; ++channel) {
            if (channel_active(channel)) {
                value = static_cast<std::uint16_t>(value | (1U << channel));
            }
        }
    } else if (aligned < kWaveRamStart) {
        value = static_cast<std::uint16_t>(register16(aligned) & read_mask(aligned));
    }
    return static_cast<std::uint8_t>((offset & 1U) != 0U ? value >> 8U : value);
}

void Apu::write(const std::uint32_t offset, const std::uint8_t value) noexcept {
    if (offset >= kWaveRamStart && offset < kWaveRamEnd) {
        wave_ram_[wave_.selected_bank ^ 1U][offset - kWaveRamStart] = value;
        return;
    }
    if (offset >= kFifoStart && offset < kFifoEnd) {
        fifos_[(offset - kFifoStart) / 4U].push(value);
        return;
    }
    switch (offset) {
    case kSoundControlX: {
        const bool was_enabled = master_enabled();
        registers_[kSoundControlX] = static_cast<std::uint8_t>(value & 0x80U);
        if (was_enabled && !master_enabled()) {
            // Turning the sound circuits off clears every PSG register.
            reset_psg();
        }
        return;
    }
    case kSoundControlX + 1U:
        return;
    case kSoundControlHigh:
        registers_[offset] = static_cast<std::uint8_t>(value & 0x0FU);
        return;
    case kSoundControlHigh + 1U:
        if ((value & 0x08U) != 0U) {
            fifos_[0].clear();
        }
        if ((value & 0x80U) != 0U) {
            fifos_[1].clear();
        }
        registers_[offset] = static_cast<std::uint8_t>(value & 0x77U);
        return;
    case kSoundBias:
    case kSoundBias + 1U:
        registers_[offset] = value;
        return;
    default:
        break;
    }
    if (offset < kSoundControlHigh) {
        if (!master_enabled()) {
            return; // PSG registers are not writable while sound is off
        }
        registers_[offset] = value;
        write_psg(offset, value);
    }
}

void Apu::write_psg(const std::uint32_t offset, const std::uint8_t value) noexcept {
    const auto set_frequency_low = [value](auto& channel) {
        channel.frequency = static_cast<std::uint16_t>((channel.frequency & 0x0700U) | value);
    };
    const auto set_frequency_high = [value](auto& channel) {
        channel.frequency =
            static_cast<std::uint16_t>((channel.frequency & 0x00FFU) | ((value & 0x07U) << 8U));
        channel.length_enable = (value & 0x40U) != 0U;
    };
    const auto load_envelope = [value](auto& channel) {
        channel.envelope.load(value);
        channel.dac = (value & 0xF8U) != 0U;
        if (!channel.dac) {
            channel.enabled = false;
        }
    };

    switch (offset) {
    case kSound1Sweep:
        square1_.sweep_shift = static_cast<std::uint8_t>(value & 0x07U);
        square1_.sweep_negate = (value & 0x08U) != 0U;
        square1_.sweep_period = static_cast<std::uint8_t>((value >> 4U) & 0x07U);
        break;
    case 0x02U:
        square1_.length = static_cast<std::uint16_t>(64U - (value & 0x3FU));
        square1_.duty = static_cast<std::uint8_t>(value >> 6U);
        break;
    case 0x03U:
        load_envelope(square1_);
        break;
    case 0x04U:
        set_frequency_low(square1_);
        break;
    case 0x05U:
        set_frequency_high(square1_);
        if ((value & 0x80U) != 0U) {
            trigger_square(square1_, true);
        }
        break;
    case 0x08U:
        square2_.length = static_cast<std::uint16_t>(64U - (value & 0x3FU));
        square2_.duty = static_cast<std::uint8_t>(value >> 6U);
        break;
    case 0x09U:
        load_envelope(square2_);
        break;
    case 0x0CU:
        set_frequency_low(square2_);
        break;
    case 0x0DU:
        set_frequency_high(square2_);
        if ((value & 0x80U) != 0U) {
            trigger_square(square2_, false);
        }
        break;
    case 0x10U:
        wave_.two_banks = (value & 0x20U) != 0U;
        wave_.selected_bank = static_cast<std::uint8_t>((value >> 6U) & 1U);
        wave_.dac = (value & 0x80U) != 0U;
        if (!wave_.dac) {
            wave_.enabled = false;
        }
        break;
    case 0x12U:
        wave_.length = static_cast<std::uint16_t>(256U - value);
        break;
    case 0x13U:
        wave_.volume_code = static_cast<std::uint8_t>((value >> 5U) & 0x03U);
        wave_.force_75_percent = (value & 0x80U) != 0U;
        break;
    case 0x14U:
        set_frequency_low(wave_);
        break;
    case 0x15U:
        set_frequency_high(wave_);
        if ((value & 0x80U) != 0U) {
            trigger_wave();
        }
        break;
    case 0x18U:
        noise_.length = static_cast<std::uint16_t>(64U - (value & 0x3FU));
        break;
    case 0x19U:
        load_envelope(noise_);
        break;
    case 0x1CU:
        noise_.divisor = static_cast<std::uint8_t>(value & 0x07U);
        noise_.narrow = (value & 0x08U) != 0U;
        noise_.shift = static_cast<std::uint8_t>(value >> 4U);
        break;
    case 0x1DU:
        noise_.length_enable = (value & 0x40U) != 0U;
        if ((value & 0x80U) != 0U) {
            trigger_noise();
        }
        break;
    default:
        break;
    }
}

void Apu::trigger_square(Square& channel, const bool has_sweep) noexcept {
    channel.enabled = channel.dac;
    if (channel.length == 0U) {
        channel.length = 64;
    }
    channel.timer = channel.period();
    channel.envelope.trigger();
    if (has_sweep) {
        channel.shadow_frequency = channel.frequency;
        channel.sweep_timer = channel.sweep_period != 0U ? channel.sweep_period : 8U;
        channel.sweep_enabled = channel.sweep_period != 0U || channel.sweep_shift != 0U;
        if (channel.sweep_shift != 0U) {
            static_cast<void>(sweep_target(channel)); // immediate overflow check
        }
    }
}

void Apu::trigger_wave() noexcept {
    wave_.enabled = wave_.dac;
    if (wave_.length == 0U) {
        wave_.length = 256;
    }
    wave_.position = 0;
    wave_.timer = wave_.period();
}

void Apu::trigger_noise() noexcept {
    noise_.enabled = noise_.dac;
    if (noise_.length == 0U) {
        noise_.length = 64;
    }
    noise_.lfsr = 0x7FFFU;
    noise_.timer = noise_.period();
    noise_.envelope.trigger();
}

std::uint16_t Apu::sweep_target(Square& channel) noexcept {
    const auto delta = static_cast<std::uint16_t>(channel.shadow_frequency >> channel.sweep_shift);
    const auto target = channel.sweep_negate
                            ? static_cast<std::uint16_t>(channel.shadow_frequency - delta)
                            : static_cast<std::uint16_t>(channel.shadow_frequency + delta);
    if (!channel.sweep_negate && target > 2047U) {
        channel.enabled = false;
    }
    return target;
}

void Apu::clock_sweep(Square& channel) noexcept {
    if (channel.sweep_timer > 0U) {
        --channel.sweep_timer;
    }
    if (channel.sweep_timer != 0U) {
        return;
    }
    channel.sweep_timer = channel.sweep_period != 0U ? channel.sweep_period : 8U;
    if (!channel.sweep_enabled || channel.sweep_period == 0U) {
        return;
    }
    const auto target = sweep_target(channel);
    if (target <= 2047U && channel.sweep_shift != 0U) {
        channel.shadow_frequency = target;
        channel.frequency = target;
        static_cast<void>(sweep_target(channel));
    }
}

std::int32_t Apu::wave_output() const noexcept {
    if (!wave_.enabled) {
        return 0;
    }
    const auto bank =
        wave_.two_banks
            ? static_cast<std::size_t>(wave_.selected_bank ^ (wave_.position >= 32U ? 1U : 0U))
            : static_cast<std::size_t>(wave_.selected_bank);
    const auto index = static_cast<std::size_t>(wave_.position % 32U);
    const auto byte = wave_ram_[bank][index / 2U];
    const auto sample = (index % 2U) == 0U ? (static_cast<unsigned>(byte) >> 4U)
                                           : (static_cast<unsigned>(byte) & 0x0FU);
    const auto centered = static_cast<std::int32_t>(sample) * 2 - 15;
    if (wave_.force_75_percent) {
        return centered * 3 / 4;
    }
    switch (wave_.volume_code) {
    case 1:
        return centered;
    case 2:
        return centered / 2;
    case 3:
        return centered / 4;
    default:
        return 0;
    }
}

std::int64_t Apu::advance_wave(std::int32_t cycles) noexcept {
    if (wave_.timer <= 0) {
        wave_.timer = wave_.period();
    }
    const auto samples = wave_.two_banks ? 64U : 32U;
    std::int64_t sum = 0;
    while (cycles > 0) {
        const auto run = std::min(cycles, wave_.timer);
        sum += static_cast<std::int64_t>(wave_output()) * run;
        wave_.timer -= run;
        cycles -= run;
        if (wave_.timer == 0) {
            wave_.timer = wave_.period();
            wave_.position = static_cast<std::uint8_t>((wave_.position + 1U) % samples);
        }
    }
    return sum;
}

void Apu::on_sequencer_step() noexcept {
    if (master_enabled()) {
        if ((sequencer_step_ & 1U) == 0U) {
            clock_length(square1_);
            clock_length(square2_);
            clock_length(wave_);
            clock_length(noise_);
        }
        if (sequencer_step_ == 2U || sequencer_step_ == 6U) {
            clock_sweep(square1_);
        }
        if (sequencer_step_ == 7U) {
            square1_.envelope.clock();
            square2_.envelope.clock();
            noise_.envelope.clock();
        }
    }
    sequencer_step_ = static_cast<std::uint8_t>((sequencer_step_ + 1U) & 7U);
}

std::uint8_t Apu::on_timer_overflow(const std::uint8_t timer_mask,
                                    const std::uint64_t timestamp) noexcept {
    if (!master_enabled()) {
        return 0;
    }
    const auto control = register16(kSoundControlHigh);
    std::uint8_t refill = 0;
    for (std::size_t index = 0; index < fifos_.size(); ++index) {
        const auto timer = (control >> (index == 0U ? 10U : 14U)) & 1U;
        if ((timer_mask & (1U << timer)) == 0U) {
            continue;
        }
        auto& fifo = fifos_[index];
        if (fifo.size > 0U) {
            if (timestamp > fifo.last_change) {
                fifo.integral += static_cast<std::int64_t>(fifo.current) *
                                 static_cast<std::int64_t>(timestamp - fifo.last_change);
                fifo.last_change = timestamp;
            }
            fifo.current = fifo.data[fifo.read_index];
            fifo.read_index = (fifo.read_index + 1U) % fifo.data.size();
            --fifo.size;
        }
        if (fifo.size <= 16U) {
            refill = static_cast<std::uint8_t>(refill | (1U << index));
        }
    }
    return refill;
}

void Apu::on_sample(const std::uint64_t timestamp) noexcept {
    // Close this sample's FIFO integration window.
    const auto window_start = timestamp >= kCyclesPerSample ? timestamp - kCyclesPerSample : 0U;
    std::array<std::int64_t, 2> fifo_averages{};
    for (std::size_t index = 0; index < fifos_.size(); ++index) {
        auto& fifo = fifos_[index];
        const auto start = std::max(fifo.last_change, window_start);
        if (timestamp > start) {
            fifo.integral += static_cast<std::int64_t>(fifo.current) *
                             static_cast<std::int64_t>(timestamp - start);
        }
        fifo_averages[index] = fifo.integral / static_cast<std::int64_t>(kCyclesPerSample);
        fifo.integral = 0;
        fifo.last_change = timestamp;
    }
    last_sample_time_ = timestamp;

    if (samples_.size() >= kMaximumBufferedFrames * 2U) {
        return;
    }
    if (!master_enabled()) {
        samples_.push_back(0);
        samples_.push_back(0);
        return;
    }

    const std::array<std::int64_t, 4> integrals{
        square1_.advance(kCyclesPerSample),
        square2_.advance(kCyclesPerSample),
        advance_wave(kCyclesPerSample),
        noise_.advance(kCyclesPerSample),
    };
    const auto routing = register16(kSoundControlLow);
    const auto mixing = register16(kSoundControlHigh);

    std::int64_t right = 0;
    std::int64_t left = 0;
    for (std::size_t channel = 0; channel < 4U; ++channel) {
        if ((routing & (1U << (8U + channel))) != 0U) {
            right += integrals[channel];
        }
        if ((routing & (1U << (12U + channel))) != 0U) {
            left += integrals[channel];
        }
    }
    // Average over the sample period, apply the PSG master volume (1-8) and the PSG/Direct
    // Sound ratio (25%, 50%, 100%).
    const std::array<std::int64_t, 4> ratio_divisors{4, 2, 1, 1};
    const auto divisor = static_cast<std::int64_t>(kCyclesPerSample) * ratio_divisors[mixing & 3U];
    right = right * ((routing & 7U) + 1U) / divisor;
    left = left * (((routing >> 4U) & 7U) + 1U) / divisor;

    for (std::size_t index = 0; index < fifos_.size(); ++index) {
        const auto full_volume = (mixing & (1U << (2U + index))) != 0U;
        const auto sample = fifo_averages[index] * (full_volume ? 2 : 1);
        const auto base = index == 0U ? 8U : 12U;
        if ((mixing & (1U << base)) != 0U) {
            right += sample;
        }
        if ((mixing & (1U << (base + 1U))) != 0U) {
            left += sample;
        }
    }

    // The hardware mixes into a 10-bit range around SOUNDBIAS; scale that to 16 bits.
    const auto to_pcm = [](const std::int64_t value) {
        return static_cast<std::int16_t>(std::clamp<std::int64_t>(value, -512, 511) * 64);
    };
    samples_.push_back(to_pcm(left));
    samples_.push_back(to_pcm(right));
}

void Apu::take_samples(std::vector<std::int16_t>& destination) {
    destination.insert(destination.end(), samples_.begin(), samples_.end());
    samples_.clear();
}

std::size_t Apu::buffered_frames() const noexcept {
    return samples_.size() / 2U;
}

bool Apu::channel_active(const std::size_t channel) const noexcept {
    switch (channel) {
    case 0:
        return square1_.enabled;
    case 1:
        return square2_.enabled;
    case 2:
        return wave_.enabled;
    case 3:
        return noise_.enabled;
    default:
        return false;
    }
}

std::size_t Apu::fifo_size(const std::size_t fifo) const noexcept {
    return fifos_[fifo].size;
}

} // namespace srgba::core
