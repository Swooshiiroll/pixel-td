cmake_minimum_required(VERSION 3.28)

# The version line is read and written by tools/dev.sh (stack_get_version / stack_set_version):
# keep "project(pixel_td VERSION x.y.z" on one line.
project(pixel_td VERSION 0.1.0
  DESCRIPTION "An endless roguelite tower defense game with retro-futuristic pixel art"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules here: skip CMake's per-file module scan (faster builds, and ccache can cache
# the compiles; it can't with the scan on).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# Static MSVC runtime: the exe runs without the player installing the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(PIXELTD_BUILD_GAME "Build the SDL3 game executable (tests only need the core library)" ON)
option(PIXELTD_WERROR "Treat compiler warnings in this project's code as errors" OFF)

include(CTest)
include(FetchContent)

# Warning flags for this project's own targets (never applied to SDL or doctest).
add_library(pixeltd_warnings INTERFACE)
if(MSVC)
  target_compile_options(pixeltd_warnings INTERFACE /W4 /permissive- /utf-8
    $<$<BOOL:${PIXELTD_WERROR}>:/WX>)
else()
  target_compile_options(pixeltd_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion
    $<$<BOOL:${PIXELTD_WERROR}>:-Werror>)
endif()

# --- Dependencies: pinned release archives, verified by SHA256 -----------------------------------
if(PIXELTD_BUILD_GAME)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(SDL3)

  # Dear ImGui (developer tuning panel, #23): no CMake project of its own, so it's built below as
  # pixeltd_imgui with the SDL3 platform and SDL_Renderer backends.
  FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.tar.gz
    URL_HASH SHA256=f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(imgui)
  add_library(pixeltd_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
  target_include_directories(pixeltd_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(pixeltd_imgui PUBLIC SDL3::SDL3-static)
endif()

if(BUILD_TESTING)
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/releases/download/v2.5.3/doctest-v2.5.3.tar.gz
    URL_HASH SHA256=eb4a39729d091e43d4d593888bf4d145ee9a0f0e13f7404484b6f2fb60d5d50f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(doctest)
endif()

# --- Project ----------------------------------------------------------------------------------------
add_subdirectory(src/core)
if(PIXELTD_BUILD_GAME)
  add_subdirectory(src/game)
endif()
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
