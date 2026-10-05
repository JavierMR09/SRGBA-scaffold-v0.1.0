// Runs public, redistributable GBA test ROMs when they are available locally.
//
// CI downloads jsmolka/gba-tests (MIT licensed) and sets SRGBA_GBA_TESTS_DIR. Each ROM runs a suite
// of hardware checks and leaves the number of the first failing check in r12 (0 = all passed).
#include "srgba/core/emulator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path suite_directory() {
#if defined(_MSC_VER)
    // MSVC deprecates getenv (C4996) in favor of _dupenv_s.
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "SRGBA_GBA_TESTS_DIR") != 0 || value == nullptr) {
        return {};
    }
    std::filesystem::path path(value);
    std::free(value);
    return path;
#else
    const char* value = std::getenv("SRGBA_GBA_TESTS_DIR");
    return value != nullptr ? std::filesystem::path(value) : std::filesystem::path{};
#endif
}

void run_suite(const char* relative_path) {
    const auto directory = suite_directory();
    if (directory.empty() || !std::filesystem::exists(directory / relative_path)) {
        SKIP("Set SRGBA_GBA_TESTS_DIR to a jsmolka/gba-tests checkout to run this suite.");
    }
    srgba::core::Emulator emulator;
    std::string error;
    REQUIRE(emulator.load_rom(directory / relative_path, error));
    for (int frame = 0; frame < 120 && !emulator.fault(); ++frame) {
        emulator.run_frame();
    }
    INFO("ROM: " << relative_path);
    REQUIRE_FALSE(emulator.fault().has_value());
    // r12 holds the first failing test number.
    REQUIRE(emulator.cpu().register_value(12) == 0U);
}

} // namespace

TEST_CASE("Public suite: ARM instructions", "[public-roms][cpu]") {
    run_suite("arm/arm.gba");
}

TEST_CASE("Public suite: Thumb instructions", "[public-roms][cpu]") {
    run_suite("thumb/thumb.gba");
}

TEST_CASE("Public suite: memory", "[public-roms][bus]") {
    run_suite("memory/memory.gba");
}

TEST_CASE("Public suite: BIOS open bus", "[public-roms][bios]") {
    run_suite("bios/bios.gba");
}

TEST_CASE("Public suite: pipeline and DMA (NES)", "[public-roms][cpu][dma]") {
    run_suite("nes/nes.gba");
}
