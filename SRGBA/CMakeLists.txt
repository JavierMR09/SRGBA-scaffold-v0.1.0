cmake_minimum_required(VERSION 3.28)

project(
    SRGBA
    VERSION 0.6.0
    DESCRIPTION "A clean-room Game Boy Advance emulator"
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

option(SRGBA_BUILD_APP "Build the SDL3 desktop application" ON)
option(SRGBA_BUILD_TESTS "Build SRGBA's tests" ${BUILD_TESTING})
option(SRGBA_WARNINGS_AS_ERRORS "Treat SRGBA compiler warnings as errors" OFF)
option(SRGBA_STATIC_MSVC_RUNTIME "Link the MSVC runtime statically" ON)

if(MSVC AND SRGBA_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

add_library(
    srgba_core
    src/core/apu.cpp
    src/core/arm7tdmi.cpp
    src/core/backup.cpp
    src/core/cartridge.cpp
    src/core/cheats.cpp
    src/core/display_filter.cpp
    src/core/dma.cpp
    src/core/emulator.cpp
    src/core/gba_bus.cpp
    src/core/hle_bios.cpp
    src/core/ppu.cpp
    src/core/rewind.cpp
    src/core/rom_library.cpp
    src/core/save_file.cpp
    src/core/system_bios.cpp
    src/core/timers.cpp
)
add_library(SRGBA::core ALIAS srgba_core)

target_compile_features(srgba_core PUBLIC cxx_std_20)
target_include_directories(
    srgba_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
srgba_set_project_warnings(srgba_core)

# Original homebrew tooling: a tiny ARM assembler and the SRGBA demo ROM used by tests and
# shipped in release packages.
add_library(
    srgba_homebrew STATIC
    tools/homebrew/arm_assembler.cpp
    tools/homebrew/demo_rom.cpp
    tools/homebrew/tiles_demo_rom.cpp
)
target_include_directories(srgba_homebrew PUBLIC "${PROJECT_SOURCE_DIR}/tools/homebrew")
target_link_libraries(srgba_homebrew PUBLIC SRGBA::core)
srgba_set_project_warnings(srgba_homebrew)

add_executable(srgba_make_demo_rom tools/homebrew/make_demo_rom.cpp)
target_link_libraries(srgba_make_demo_rom PRIVATE srgba_homebrew)
srgba_set_project_warnings(srgba_make_demo_rom)

if(SRGBA_BUILD_APP)
    include(Dependencies)
    srgba_fetch_frontend_dependencies()

    add_executable(
        srgba_app WIN32
        src/app/application.cpp
        src/app/input.cpp
        src/app/main.cpp
        src/app/settings.cpp
    )
    set_target_properties(srgba_app PROPERTIES OUTPUT_NAME SRGBA)
    target_compile_features(srgba_app PRIVATE cxx_std_20)
    target_include_directories(srgba_app PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(srgba_app PRIVATE SRGBA_VERSION="${PROJECT_VERSION}")
    target_link_libraries(
        srgba_app
        PRIVATE
            SRGBA::core
            SDL3::SDL3
            nlohmann_json::nlohmann_json
            srgba_imgui
    )
    if(WIN32)
        target_compile_definitions(srgba_app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    srgba_set_project_warnings(srgba_app)

    install(TARGETS srgba_app RUNTIME DESTINATION .)
    install(FILES README.md LICENSE DESTINATION .)

    set(SRGBA_SAMPLES_DIR "${CMAKE_BINARY_DIR}/samples")
    set(SRGBA_DEMO_ROMS "${SRGBA_SAMPLES_DIR}/SRGBA-demo.gba" "${SRGBA_SAMPLES_DIR}/SRGBA-tiles-demo.gba")
    add_custom_command(
        OUTPUT ${SRGBA_DEMO_ROMS}
        COMMAND srgba_make_demo_rom "${SRGBA_SAMPLES_DIR}"
        DEPENDS srgba_make_demo_rom
        COMMENT "Generating the SRGBA demo ROMs"
        VERBATIM
    )
    add_custom_target(srgba_demo_rom ALL DEPENDS ${SRGBA_DEMO_ROMS})
    install(FILES ${SRGBA_DEMO_ROMS} DESTINATION samples)
    install(
        FILES "${SRGBA_SDL_SOURCE_DIR}/LICENSE.txt"
        DESTINATION licenses
        RENAME SDL3.txt
    )
    install(
        FILES "${SRGBA_IMGUI_SOURCE_DIR}/LICENSE.txt"
        DESTINATION licenses
        RENAME Dear-ImGui.txt
    )
    install(
        FILES "${SRGBA_JSON_SOURCE_DIR}/LICENSE.MIT"
        DESTINATION licenses
        RENAME nlohmann-json.txt
    )

    if(WIN32)
        set(SRGBA_PACKAGE_PLATFORM "windows-x64")
    elseif(APPLE)
        set(SRGBA_PACKAGE_PLATFORM "macos")
    else()
        set(SRGBA_PACKAGE_PLATFORM "linux-x64")
    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "SRGBA")
    set(CPACK_PACKAGE_VENDOR "SRGBA contributors")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "SRGBA-${PROJECT_VERSION}-${SRGBA_PACKAGE_PLATFORM}")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    include(CPack)
endif()

if(SRGBA_BUILD_TESTS)
    include(Dependencies)
    srgba_fetch_test_dependencies()

    add_executable(
        srgba_tests
        tests/arm7tdmi_state_tests.cpp
        tests/apu_tests.cpp
        tests/arm_extended_instruction_tests.cpp
        tests/arm_instruction_tests.cpp
        tests/backup_tests.cpp
        tests/cheat_tests.cpp
        tests/demo_rom_tests.cpp
        tests/display_library_tests.cpp
        tests/emulator_tests.cpp
        tests/gba_bus_tests.cpp
        tests/hle_bios_tests.cpp
        tests/interrupt_keypad_tests.cpp
        tests/memory_instruction_tests.cpp
        tests/ppu_scene_tests.cpp
        tests/public_test_rom_tests.cpp
        tests/ppu_tests.cpp
        tests/rom_header_tests.cpp
        tests/save_state_tests.cpp
        tests/scheduler_tests.cpp
        tests/thumb_instruction_tests.cpp
        tests/timer_dma_tests.cpp
    )
    target_compile_features(srgba_tests PRIVATE cxx_std_20)
    target_include_directories(srgba_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(srgba_tests PRIVATE SRGBA::core srgba_homebrew Catch2::Catch2WithMain)
    srgba_set_project_warnings(srgba_tests)

    include(Catch)
    catch_discover_tests(srgba_tests)
endif()
