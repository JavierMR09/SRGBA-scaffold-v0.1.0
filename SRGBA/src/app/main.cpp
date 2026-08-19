#include "app/application.hpp"

#include <SDL3/SDL_main.h>

int main(int argc, char** argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    srgba::app::Application application;
    return application.run();
}
