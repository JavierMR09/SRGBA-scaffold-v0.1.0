// Writes SRGBA's original demo ROMs into the directory given on the command line.
#include "demo_rom.hpp"
#include "tiles_demo_rom.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

bool write_file(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        std::fprintf(stderr, "could not write %s\n", path.string().c_str());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: srgba_make_demo_rom <output-directory>\n");
        return 2;
    }
    try {
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        if (!write_file(directory / "SRGBA-demo.gba", srgba::homebrew::build_demo_rom()) ||
            !write_file(directory / "SRGBA-tiles-demo.gba",
                        srgba::homebrew::build_tiles_demo_rom())) {
            return 1;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "failed to build demo ROMs: %s\n", error.what());
        return 1;
    }
    return 0;
}
