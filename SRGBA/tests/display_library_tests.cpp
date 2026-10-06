#include "test_helpers.hpp"

#include "srgba/core/display_filter.hpp"
#include "srgba/core/emulator.hpp"
#include "srgba/core/rom_library.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using srgba::core::ColorCorrection;
using srgba::core::Framebuffer;
using srgba::core::LcdFilter;
using srgba::core::Rgba8;

[[nodiscard]] Rgba8 expanded(const unsigned red, const unsigned green, const unsigned blue) {
    const auto expand = [](const unsigned channel) {
        return static_cast<std::uint8_t>((channel << 3U) | (channel >> 2U));
    };
    return Rgba8{expand(red), expand(green), expand(blue), 255};
}

void write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("LCD color correction darkens midtones and blends primaries", "[display]") {
    const ColorCorrection correction;
    CHECK(correction.apply(expanded(31, 31, 31)) == Rgba8{255, 255, 255, 255});
    CHECK(correction.apply(expanded(0, 0, 0)) == Rgba8{0, 0, 0, 255});

    const auto gray = expanded(16, 16, 16);
    const auto corrected_gray = correction.apply(gray);
    CHECK(corrected_gray.red < gray.red);
    CHECK(corrected_gray.red == corrected_gray.green);
    CHECK(corrected_gray.green == corrected_gray.blue);

    const auto red = correction.apply(expanded(31, 0, 0));
    CHECK(red.red < 255U);
    CHECK(red.green > 0U);
    CHECK(red.red > red.green);
}

TEST_CASE("Display filters upscale with an LCD grid or scanlines", "[display]") {
    Framebuffer source{};
    source.fill(Rgba8{200, 100, 50, 255});
    std::vector<Rgba8> output;

    srgba::core::render_display(source, nullptr, LcdFilter::None, 1, 1.0F, output);
    REQUIRE(output.size() == source.size());
    CHECK(std::equal(output.begin(), output.end(), source.begin()));

    srgba::core::render_display(source, nullptr, LcdFilter::Grid, 3, 1.0F, output);
    REQUIRE(output.size() == 720U * 480U);
    const auto at = [&](const std::size_t x, const std::size_t y) { return output[y * 720U + x]; };
    CHECK(at(0, 0) == source[0]);
    CHECK(at(1, 1) == source[0]);
    CHECK(at(2, 0).red < 200U);         // right edge of the first pixel
    CHECK(at(0, 2).red < 200U);         // bottom edge
    CHECK(at(2, 2).red < at(2, 0).red); // corner is darkest
    CHECK(at(3, 0) == source[0]);       // next pixel starts bright again

    srgba::core::render_display(source, nullptr, LcdFilter::Scanlines, 3, 1.0F, output);
    CHECK(at(0, 0) == source[0]);
    CHECK(at(2, 1) == source[0]);
    CHECK(at(0, 2).red < 200U);

    // At zero strength the filters are a plain nearest-neighbor upscale.
    srgba::core::render_display(source, nullptr, LcdFilter::Grid, 4, 0.0F, output);
    CHECK(std::all_of(output.begin(), output.end(),
                      [&](const Rgba8 pixel) { return pixel == source[0]; }));

    const ColorCorrection correction;
    srgba::core::render_display(source, &correction, LcdFilter::None, 2, 1.0F, output);
    CHECK(output[0] == correction.apply(source[0]));
}

TEST_CASE("The ROM library finds ROM files and reads their metadata", "[library]") {
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto folder =
        std::filesystem::temp_directory_path() / ("srgba-library-" + std::to_string(stamp));
    std::filesystem::create_directories(folder / "More");
    const auto rom = srgba::tests::make_valid_test_rom();
    write_file(folder / "Alpha.gba", rom);
    write_file(folder / "beta.GBA", rom);
    write_file(folder / "More" / "gamma.agb", rom);
    write_file(folder / "notes.txt", {1, 2, 3});
    write_file(folder / "tiny.gba", {1, 2, 3});

    const auto top = srgba::core::find_rom_files(folder, false);
    REQUIRE(top.size() == 3U);
    CHECK(top[0].filename() == "Alpha.gba");
    CHECK(srgba::core::find_rom_files(folder, true).size() == 4U);
    CHECK(srgba::core::find_rom_files(folder, true, 2).size() == 2U);
    CHECK(srgba::core::find_rom_files(folder / "missing", true).empty());

    std::string error;
    const auto info = srgba::core::read_rom_info(folder / "Alpha.gba", error);
    REQUIRE(info.has_value());
    CHECK(info->title == "SRGBA TEST");
    CHECK(info->game_code == "SRGE");
    CHECK(info->maker_code == "01");
    CHECK(info->size == rom.size());
    CHECK(info->header_valid);
    CHECK(info->save_type == srgba::core::SaveType::Sram); // untagged ROMs fall back to SRAM

    CHECK_FALSE(srgba::core::read_rom_info(folder / "tiny.gba", error).has_value());
    CHECK_FALSE(error.empty());

    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}

TEST_CASE("Frame advance runs exactly one frame while paused", "[emulator]") {
    const srgba::tests::TemporaryRom rom(srgba::tests::make_valid_test_rom());
    srgba::core::Emulator emulator;
    emulator.set_battery_saves_enabled(false);
    std::string error;
    REQUIRE(emulator.load_rom(rom.path(), error));

    emulator.advance_frame(); // ignored while running
    CHECK(emulator.frame_counter() == 0U);
    emulator.set_paused(true);
    emulator.advance_frame();
    emulator.advance_frame();
    CHECK(emulator.frame_counter() == 2U);
    CHECK(emulator.is_paused());
    emulator.run_frame(); // paused games do not run
    CHECK(emulator.frame_counter() == 2U);
}
