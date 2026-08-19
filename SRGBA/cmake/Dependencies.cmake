include_guard(GLOBAL)
include(FetchContent)

set(FETCHCONTENT_QUIET OFF)

function(srgba_fetch_frontend_dependencies)
    if(TARGET SDL3::SDL3 AND TARGET srgba_imgui AND TARGET nlohmann_json::nlohmann_json)
        return()
    endif()

    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(SDL_DISABLE_INSTALL_DOCS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.14
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9b
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )

    FetchContent_MakeAvailable(SDL3 imgui nlohmann_json)

    if(NOT TARGET srgba_imgui)
        add_library(
            srgba_imgui STATIC
            "${imgui_SOURCE_DIR}/imgui.cpp"
            "${imgui_SOURCE_DIR}/imgui_draw.cpp"
            "${imgui_SOURCE_DIR}/imgui_tables.cpp"
            "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
            "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
            "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp"
        )
        target_compile_features(srgba_imgui PUBLIC cxx_std_20)
        target_include_directories(
            srgba_imgui
            PUBLIC
                "${imgui_SOURCE_DIR}"
                "${imgui_SOURCE_DIR}/backends"
        )
        target_link_libraries(srgba_imgui PUBLIC SDL3::SDL3)
        if(MSVC)
            target_compile_options(srgba_imgui PRIVATE /W3)
        else()
            target_compile_options(srgba_imgui PRIVATE -w)
        endif()
    endif()

    set(SRGBA_SDL_SOURCE_DIR "${sdl3_SOURCE_DIR}" PARENT_SCOPE)
    set(SRGBA_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}" PARENT_SCOPE)
    set(SRGBA_JSON_SOURCE_DIR "${nlohmann_json_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(srgba_fetch_test_dependencies)
    if(TARGET Catch2::Catch2WithMain)
        return()
    endif()

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.15.3
        GIT_SHALLOW TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
    set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" PARENT_SCOPE)
endfunction()

