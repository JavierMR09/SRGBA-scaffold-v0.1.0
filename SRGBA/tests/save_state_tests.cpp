#include "test_helpers.hpp"

#include "demo_rom.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/interrupts.hpp"
#include "srgba/core/rewind.hpp"
#include "tiles_demo_rom.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace {

using srgba::core::Emulator;
using srgba::core::Key;
using srgba::core::RewindBuffer;

struct Recording {
    std::vector<srgba::core::Rgba8> frames;
    std::vector<std::int16_t> audio;
};

// Plays a fixed input script so two runs can be compared exactly.
Recording play(Emulator& emulator, const int frames) {
    Recording recording;
    for (int frame = 0; frame < frames; ++frame) {
        std::uint16_t keys = 0;
        if (frame % 20 < 10) {
            keys = keys | Key::Right;
        }
        if (frame % 15 == 0) {
            keys = keys | Key::A;
        }
        if (frame % 25 == 3) {
            keys = keys | Key::B;
        }
        emulator.set_pressed_keys(keys);
        emulator.run_frame();
        const auto& framebuffer = emulator.framebuffer();
        recording.frames.insert(recording.frames.end(), framebuffer.begin(), framebuffer.end());
        emulator.take_audio_samples(recording.audio);
    }
    return recording;
}

[[nodiscard]] std::filesystem::path unique_directory() {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("srgba-states-" + std::to_string(stamp));
}

// Index of the first differing byte, or the shorter length when one is a prefix (-1 if equal).
// Comparing this keeps failure output readable for ~400 KiB states.
[[nodiscard]] long long first_difference(const std::vector<std::uint8_t>& left,
                                         const std::vector<std::uint8_t>& right) {
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (left[index] != right[index]) {
            return static_cast<long long>(index);
        }
    }
    return left.size() == right.size() ? -1 : static_cast<long long>(count);
}

void load(Emulator& emulator, const srgba::tests::TemporaryRom& rom) {
    emulator.set_battery_saves_enabled(false);
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));
}

} // namespace

TEST_CASE("Loading a save state replays identically", "[savestate][determinism]") {
    const srgba::tests::TemporaryRom rom(srgba::homebrew::build_tiles_demo_rom());
    Emulator emulator;
    load(emulator, rom);
    static_cast<void>(play(emulator, 30));

    const auto state = emulator.save_state();
    REQUIRE(state.size() > 300000U);
    const auto first = play(emulator, 40);
    const auto frame_after_first = emulator.frame_counter();

    std::string error;
    REQUIRE(emulator.load_state(state, error));
    REQUIRE(error.empty());
    REQUIRE(emulator.frame_counter() == 30U);
    const auto second = play(emulator, 40);
    REQUIRE(emulator.frame_counter() == frame_after_first);
    REQUIRE(first.frames.size() == second.frames.size());
    REQUIRE(std::equal(first.frames.begin(), first.frames.end(), second.frames.begin()));
    REQUIRE(first.audio.size() == second.audio.size());
    REQUIRE(std::equal(first.audio.begin(), first.audio.end(), second.audio.begin()));
    REQUIRE_FALSE(first.audio.empty());
}

TEST_CASE("Save states round-trip byte for byte", "[savestate]") {
    const srgba::tests::TemporaryRom rom(srgba::homebrew::build_tiles_demo_rom());
    Emulator emulator;
    load(emulator, rom);
    static_cast<void>(play(emulator, 12));
    const auto state = emulator.save_state();
    std::string error;
    REQUIRE(emulator.load_state(state, error));
    REQUIRE(first_difference(emulator.save_state(), state) == -1);
}

TEST_CASE("Foreign and damaged save states are rejected without side effects", "[savestate]") {
    const srgba::tests::TemporaryRom tiles(srgba::homebrew::build_tiles_demo_rom());
    const srgba::tests::TemporaryRom demo(srgba::homebrew::build_demo_rom());
    Emulator other;
    load(other, demo);
    static_cast<void>(play(other, 5));
    const auto foreign = other.save_state();

    Emulator emulator;
    load(emulator, tiles);
    static_cast<void>(play(emulator, 20));
    const auto before = emulator.save_state();

    std::string error;
    REQUIRE_FALSE(emulator.load_state(foreign, error));
    REQUIRE(error.find("different game") != std::string::npos);
    REQUIRE(first_difference(emulator.save_state(), before) == -1);

    auto truncated = before;
    truncated.resize(truncated.size() / 2U);
    REQUIRE_FALSE(emulator.load_state(truncated, error));
    REQUIRE(error.find("damaged") != std::string::npos);
    REQUIRE(first_difference(emulator.save_state(), before) == -1);

    auto corrupted = before;
    corrupted[200] ^= 0xFFU;   // inside the CPU section: still parses, so it loads...
    corrupted.back() ^= 0xFFU; // ...but the end marker no longer matches
    REQUIRE_FALSE(emulator.load_state(corrupted, error));
    REQUIRE(first_difference(emulator.save_state(), before) == -1);

    const std::vector<std::uint8_t> garbage(64, 0x42);
    REQUIRE_FALSE(emulator.load_state(garbage, error));
    REQUIRE(error.find("not an SRGBA save state") != std::string::npos);
}

TEST_CASE("State slots are files beside the game", "[savestate][slots]") {
    const auto directory = unique_directory();
    const srgba::tests::TemporaryRom rom(srgba::homebrew::build_tiles_demo_rom());
    Emulator emulator;
    emulator.set_save_directory(directory);
    load(emulator, rom);
    static_cast<void>(play(emulator, 25));

    std::string error;
    REQUIRE_FALSE(emulator.load_state_slot(3, error));
    REQUIRE(error.find("empty") != std::string::npos);
    REQUIRE(emulator.save_state_slot(3, error));
    const auto path = emulator.state_slot_path(3);
    REQUIRE(path.parent_path() == directory);
    REQUIRE(path.extension() == ".ss3");
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(emulator.state_slot_path(0).empty());
    REQUIRE(emulator.state_slot_path(10).empty());

    static_cast<void>(play(emulator, 15));
    REQUIRE(emulator.load_state_slot(3, error));
    REQUIRE(emulator.frame_counter() == 25U);

    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
}

TEST_CASE("Save states include save-chip contents", "[savestate][saves]") {
    const srgba::tests::TemporaryRom rom(srgba::homebrew::build_tiles_demo_rom());
    Emulator emulator;
    load(emulator, rom);
    static_cast<void>(emulator.bus().write8(0x0E000000U, 0x11U));
    const auto state = emulator.save_state();
    static_cast<void>(emulator.bus().write8(0x0E000000U, 0x22U));
    std::string error;
    REQUIRE(emulator.load_state(state, error));
    REQUIRE(emulator.bus().read8(0x0E000000U).value == 0x11U);
    REQUIRE(emulator.save_pending()); // the next autosave writes the restored contents
}

TEST_CASE("Rewind steps back through recent frames", "[rewind]") {
    const srgba::tests::TemporaryRom rom(srgba::homebrew::build_tiles_demo_rom());
    Emulator emulator;
    load(emulator, rom);
    emulator.set_rewind_enabled(true);

    for (int frame = 0; frame < 20; ++frame) {
        emulator.run_frame();
    }
    emulator.set_pressed_keys(static_cast<std::uint16_t>(Key::Right));
    for (int frame = 0; frame < 40; ++frame) {
        emulator.run_frame();
    }
    emulator.set_pressed_keys(0);
    const auto x_after = emulator.bus().read32(srgba::homebrew::kTilesDemoBallXAddress).value;
    REQUIRE(x_after > srgba::homebrew::kTilesDemoStartX + 60U);
    REQUIRE(emulator.rewind_depth() == 30U);

    for (int step = 0; step < 10; ++step) {
        REQUIRE(emulator.rewind_step());
    }
    const auto x_rewound = emulator.bus().read32(srgba::homebrew::kTilesDemoBallXAddress).value;
    REQUIRE(x_rewound < x_after);
    REQUIRE(x_rewound > srgba::homebrew::kTilesDemoStartX);
    REQUIRE(emulator.rewind_depth() == 20U);

    while (emulator.rewind_step()) {
    }
    REQUIRE(emulator.rewind_depth() == 0U);
    REQUIRE(emulator.bus().read32(srgba::homebrew::kTilesDemoBallXAddress).value ==
            srgba::homebrew::kTilesDemoStartX);
    REQUIRE_FALSE(emulator.fault().has_value());
}

TEST_CASE("The rewind buffer stores compressed differences and honors its budget", "[rewind]") {
    std::mt19937 random(1234);
    std::vector<std::vector<std::uint8_t>> snapshots;
    std::vector<std::uint8_t> snapshot(100000);
    for (auto& byte : snapshot) {
        byte = static_cast<std::uint8_t>(random());
    }
    RewindBuffer buffer;
    for (int index = 0; index < 50; ++index) {
        for (int change = 0; change < 200; ++change) {
            snapshot[random() % snapshot.size()] = static_cast<std::uint8_t>(random());
        }
        snapshots.push_back(snapshot);
        buffer.push(snapshot);
    }
    REQUIRE(buffer.size() == 50U);
    // One full snapshot plus small differences.
    REQUIRE(buffer.memory_used() < snapshot.size() + 50U * 5000U);
    for (int index = 49; index >= 0; --index) {
        const auto restored = buffer.pop();
        REQUIRE(restored.has_value());
        REQUIRE(*restored == snapshots[static_cast<std::size_t>(index)]);
    }
    REQUIRE_FALSE(buffer.pop().has_value());

    RewindBuffer small(snapshot.size() + 10000U);
    for (const auto& item : snapshots) {
        small.push(item);
    }
    REQUIRE(small.size() < 50U);
    REQUIRE(small.memory_used() <= snapshot.size() + 10000U);
    REQUIRE(*small.pop() == snapshots.back());
}
