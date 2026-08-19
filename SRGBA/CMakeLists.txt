cmake_minimum_required(VERSION 3.28)

project(
    SRGBA
    VERSION 0.2.0
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
    src/core/arm7tdmi.cpp
    src/core/cartridge.cpp
    src/core/emulator.cpp
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

if(SRGBA_BUILD_APP)
    include(Dependencies)
    srgba_fetch_frontend_dependencies()

    add_executable(
        srgba_app WIN32
        src/app/application.cpp
        src/app/main.cpp
        src/app/settings.cpp
    )
    set_target_properties(srgba_app PROPERTIES OUTPUT_NAME SRGBA)
    target_compile_features(srgba_app PRIVATE cxx_std_20)
    target_include_directories(srgba_app PRIVATE "${PROJECT_SOURCE_DIR}/src")
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
        tests/arm_instruction_tests.cpp
        tests/emulator_tests.cpp
        tests/rom_header_tests.cpp
        tests/thumb_instruction_tests.cpp
    )
    target_compile_features(srgba_tests PRIVATE cxx_std_20)
    target_include_directories(srgba_tests PRIVATE "${PROJECT_SOURCE_DIR}/tests")
    target_link_libraries(srgba_tests PRIVATE SRGBA::core Catch2::Catch2WithMain)
    srgba_set_project_warnings(srgba_tests)

    include(Catch)
    catch_discover_tests(srgba_tests)
endif()
