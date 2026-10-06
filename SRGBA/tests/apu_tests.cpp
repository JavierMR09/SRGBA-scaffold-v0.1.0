#include "test_helpers.hpp"

#include "srgba/core/apu.hpp"
#include "srgba/core/emulator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using srgba::core::Apu;
using srgba::core::Emulator;

// An emulator running an idle loop, with sound registers driven directly by the test.
class SoundFixture {
  public:
    SoundFixture() : rom_(srgba::tests::make_m2_cpu_test_rom()) {
        emulator.set_battery_saves_enabled(false);
        std::string error;
        static_cast<void>(emulator.load_rom(rom_.path(), error));
        write16(0x084, 0x0080); // SOUNDCNT_X: master enable
    }

    void write16(const std::uint32_t io_offset, const std::uint16_t value) {
        static_cast<void>(emulator.bus().write16(0x04000000U + io_offset, value));
    }
    void write32(const std::uint32_t io_offset, const std::uint32_t value) {
        static_cast<void>(emulator.bus().write32(0x04000000U + io_offset, value));
    }
    [[nodiscard]] std::uint16_t read16(const std::uint32_t io_offset) {
        return static_cast<std::uint16_t>(emulator.bus().read16(0x04000000U + io_offset).value);
    }

    // Runs whole frames and returns the left channel.
    std::vector<std::int16_t> run(const int frames) {
        std::vector<std::int16_t> stereo;
        for (int frame = 0; frame < frames; ++frame) {
            emulator.run_frame();
            emulator.take_audio_samples(stereo);
        }
        std::vector<std::int16_t> left;
        for (std::size_t index = 0; index < stereo.size(); index += 2U) {
            left.push_back(stereo[index]);
        }
        return left;
    }

    Emulator emulator;

  private:
    srgba::tests::TemporaryRom rom_;
};

[[nodiscard]] double rms(const std::vector<std::int16_t>& samples) {
    double sum = 0.0;
    for (const auto sample : samples) {
        sum += static_cast<double>(sample) * sample;
    }
    return samples.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(samples.size()));
}

// Estimates frequency from rising zero crossings.
[[nodiscard]] double frequency(const std::vector<std::int16_t>& samples) {
    std::size_t crossings = 0;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        if (samples[index - 1U] < 0 && samples[index] >= 0) {
            ++crossings;
        }
    }
    return static_cast<double>(crossings) * Apu::kSampleRate / static_cast<double>(samples.size());
}

} // namespace

TEST_CASE("Audio is produced at 32768 Hz in stereo", "[apu]") {
    SoundFixture sound;
    const auto left = sound.run(60);
    // 60 frames of 280,896 cycles at 512 cycles per sample.
    REQUIRE(left.size() == 60U * 280896U / 512U);
    REQUIRE(rms(left) == 0.0); // nothing playing yet
}

TEST_CASE("Square channel 2 plays the programmed pitch and duty", "[apu][psg]") {
    SoundFixture sound;
    sound.write16(0x080, 0xFF77);        // all channels, both sides, volume 7
    sound.write16(0x082, 0x0002);        // PSG at 100%
    sound.write16(0x068, 0xF080);        // 50% duty, volume 15, no envelope
    sound.write16(0x06C, 0x8000 | 1917); // trigger, ~1000 Hz
    REQUIRE((sound.read16(0x084) & 0x0002U) != 0U);
    const auto left = sound.run(60);
    REQUIRE(std::abs(frequency(left) - 1000.5) < 5.0);
    // One channel at volume 15 with master volume 8 spans 120/512 of full scale.
    REQUIRE(rms(left) > 7000.0);
    REQUIRE(sound.read16(0x068) == 0xF080); // duty/envelope read back; length is write-only
}

TEST_CASE("Envelopes fade and length counters stop channels", "[apu][psg]") {
    SoundFixture sound;
    sound.write16(0x080, 0xFF77);
    sound.write16(0x082, 0x0002);
    sound.write16(0x068, 0xF180); // volume 15, decrease every 1/64 s
    sound.write16(0x06C, 0x8000 | 1917);
    const auto first = sound.run(5);
    REQUIRE(rms(first) > 0.0);
    static_cast<void>(sound.run(15)); // 15 steps of 1/64 s reach volume 0
    const auto later = sound.run(10);
    REQUIRE(rms(later) == 0.0);
    REQUIRE((sound.read16(0x084) & 0x0002U) != 0U); // still "on", just silent

    sound.write16(0x062, 0xF0BF);        // channel 1: length 1 (64 - 63), volume 15
    sound.write16(0x064, 0xC000 | 1917); // trigger with length enabled
    REQUIRE((sound.read16(0x084) & 0x0001U) != 0U);
    static_cast<void>(sound.run(2));
    REQUIRE((sound.read16(0x084) & 0x0001U) == 0U);
}

TEST_CASE("Channel 1 sweep raises the pitch until it overflows", "[apu][psg]") {
    SoundFixture sound;
    sound.write16(0x080, 0xFF77);
    sound.write16(0x082, 0x0002);
    sound.write16(0x060, 0x0011); // sweep period 1, shift 1, increase
    sound.write16(0x062, 0xF080);
    sound.write16(0x064, 0x8000 | 1024);
    REQUIRE((sound.read16(0x084) & 0x0001U) != 0U);
    static_cast<void>(sound.run(10)); // 1024 -> 1536 -> 1792 -> ... -> > 2047
    REQUIRE((sound.read16(0x084) & 0x0001U) == 0U);
}

TEST_CASE("The wave channel plays wave RAM and the CPU sees the other bank", "[apu][psg]") {
    SoundFixture sound;
    sound.write16(0x070, 0x0040); // select bank 1 for playback: the CPU writes bank 0
    for (std::uint32_t offset = 0; offset < 16U; offset += 2U) {
        sound.write16(0x090 + offset, offset < 8U ? 0xFFFF : 0x0000); // square-ish wave
    }
    REQUIRE(sound.read16(0x090) == 0xFFFF);
    sound.write16(0x070, 0x0080);           // play bank 0, DAC on
    REQUIRE(sound.read16(0x090) == 0x0000); // now the CPU sees bank 1
    sound.write16(0x080, 0xFF77);
    sound.write16(0x082, 0x0002);
    sound.write16(0x072, 0x2000);        // 100% volume
    sound.write16(0x074, 0x8000 | 1984); // 65536 / (2048 - 1984) = 1024 Hz
    const auto left = sound.run(30);
    REQUIRE(std::abs(frequency(left) - 1024.0) < 5.0);
    REQUIRE(rms(left) > 7000.0);
}

TEST_CASE("The noise channel produces broadband output", "[apu][psg]") {
    SoundFixture sound;
    sound.write16(0x080, 0xFF77);
    sound.write16(0x082, 0x0002);
    sound.write16(0x078, 0xF000);
    sound.write16(0x07C, 0x8000 | 0x0010); // trigger, divisor 0, shift 1
    const auto left = sound.run(30);
    REQUIRE(rms(left) > 1000.0);
    REQUIRE(frequency(left) > 2000.0);
}

TEST_CASE("Disabling the master switch clears the PSG registers", "[apu]") {
    SoundFixture sound;
    sound.write16(0x080, 0xFF77);
    sound.write16(0x068, 0xF080);
    sound.write16(0x084, 0x0000);
    REQUIRE(sound.read16(0x080) == 0x0000);
    REQUIRE(sound.read16(0x068) == 0x0000);
    sound.write16(0x080, 0xFF77); // ignored while sound is off
    REQUIRE(sound.read16(0x080) == 0x0000);
    REQUIRE(sound.read16(0x088) == 0x0200); // SOUNDBIAS keeps its value
}

TEST_CASE("Direct Sound plays FIFO samples refilled by DMA on timer overflow",
          "[apu][dma][timers]") {
    SoundFixture sound;
    auto& bus = sound.emulator.bus();
    // A square wave at 8192 Hz playback: 32 samples of +100, 32 of -100, ... (enough data for
    // the 10 frames played below).
    for (std::uint32_t index = 0; index < 4096U; ++index) {
        static_cast<void>(
            bus.write8(0x02000000U + index, ((index / 32U) % 2U) == 0U ? 100U : 156U));
    }
    sound.write16(0x082, 0x0B0E);           // FIFO A: 100%, both sides, timer 0, reset
    sound.write32(0x0BC, 0x02000000U);      // DMA1SAD
    sound.write32(0x0C0, 0x040000A0U);      // DMA1DAD = FIFO A
    sound.write32(0x0C4, 0xB6400000U | 4U); // repeat, 32-bit, special timing, enable
    sound.write16(0x100, static_cast<std::uint16_t>(65536U - 2048U)); // 8192 Hz
    sound.write16(0x102, 0x0080);

    const auto left = sound.run(10);
    REQUIRE(rms(left) > 12000.0); // +-100 at 100% volume = +-200 of +-512 full scale
    // 64-sample cycles at 8192 Hz = 128 Hz.
    REQUIRE(std::abs(frequency(left) - 128.0) < 3.0);
    REQUIRE(bus.apu().fifo_size(0) <= 32U);
}
