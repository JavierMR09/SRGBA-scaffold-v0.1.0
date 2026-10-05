// Writes the SRGBA demo homebrew ROM to the path given on the command line.
#include "demo_rom.hpp"

#include <cstdio>
#include <exception>
#include <fstream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: srgba_make_demo_rom <output.gba>\n");
        return 2;
    }
    try {
        const auto rom = srgba::homebrew::build_demo_rom();
        std::ofstream output(argv[1], std::ios::binary);
        output.write(reinterpret_cast<const char*>(rom.data()),
                     static_cast<std::streamsize>(rom.size()));
        if (!output) {
            std::fprintf(stderr, "could not write %s\n", argv[1]);
            return 1;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "failed to build demo ROM: %s\n", error.what());
        return 1;
    }
    return 0;
}
