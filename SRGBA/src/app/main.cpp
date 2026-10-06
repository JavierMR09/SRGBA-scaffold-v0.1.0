#include "app/application.hpp"
#include "app/paths.hpp"

#include <SDL3/SDL_main.h>

#include <filesystem>
#include <optional>

int main(int argc, char** argv) {
    // A ROM passed on the command line (or dropped onto SRGBA.exe) opens at startup. SDL hands
    // the arguments over as UTF-8 on every platform.
    std::optional<std::filesystem::path> initial_rom;
    if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
        initial_rom = srgba::app::path_from_utf8(argv[1]);
    }

    srgba::app::Application application;
    return application.run(initial_rom);
}
