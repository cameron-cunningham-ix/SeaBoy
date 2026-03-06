cmake_minimum_required(VERSION 3.21) # 3.21+ for TARGET_RUNTIME_DLLS
project(SeaBoy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -----------------------------
# Default to Release for single-config generators (Ninja/Make)
# -----------------------------
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# -----------------------------
# SDL3
# -----------------------------
# Build shared so we get SDL3.dll on Windows (or .so/.dylib elsewhere)
set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG main
)
FetchContent_MakeAvailable(SDL3) # Provides SDL3::SDL3 and SDL3::SDL3main

# -----------------------------
# Dear ImGui (custom target with SDL3/SDL_Renderer backends)
# -----------------------------
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG docking
)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
  FetchContent_Populate(imgui)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp

    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  )

  target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
  )
  target_link_libraries(imgui PUBLIC SDL3::SDL3)
endif()

# -----------------------------
# Native File Dialog Extended
# -----------------------------
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG 86d5f2005fe1c00747348a12070fec493ea2407e    # v1.2.1
)
FetchContent_MakeAvailable(nfd)

# -----------------------------
# nlohmann/json (header-only, used by sm83_runner)
# -----------------------------
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# -----------------------------
# stb (header-only) – used by mealybug_runner for PNG writing
# stb has no CMakeLists.txt so we use FetchContent_Populate directly.
# -----------------------------
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
  FetchContent_Populate(stb)
endif()

# -----------------------------
# Executable
# -----------------------------
add_executable(SeaBoy
  main.cpp
  src/core/MMU.cpp
  src/core/CPU.cpp
  src/core/GameBoy.cpp
  src/core/Timer.cpp
  src/core/PPU.cpp
  src/core/OAMScan.cpp
  src/core/PixelFetcher.cpp
  src/core/Palettes.cpp
  src/core/opcodes/Opcodes.cpp
  src/core/opcodes/OpcodesCB.cpp
  src/cartridge/Cartridge.cpp
  src/cartridge/MBC0.cpp
  src/cartridge/MBC1.cpp
  src/cartridge/MBC2.cpp
)

target_include_directories(SeaBoy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(SeaBoy PRIVATE SDL3::SDL3 imgui nfd)
if(TARGET SDL3::SDL3main)
  target_link_libraries(SeaBoy PRIVATE SDL3::SDL3main)
endif()

# Warnings
if(MSVC)
  target_compile_options(SeaBoy PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
  target_compile_options(SeaBoy PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Nice output layout (esp. on MSVC multi-config)
set_target_properties(SeaBoy PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# -----------------------------
# Auto-copy runtime DLLs next to the exe (Windows/MSVC friendly)
# Works via generator expressions (CMake 3.21+)
# -----------------------------
add_custom_command(TARGET SeaBoy POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:SeaBoy>
          $<TARGET_FILE_DIR:SeaBoy>
  COMMAND_EXPAND_LISTS
)

# -----------------------------
# Include fonts
# -----------------------------
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/fonts/Roboto/Roboto-Regular.ttf
               ${CMAKE_CURRENT_BINARY_DIR}/fonts/Roboto/Roboto-Regular.ttf
               COPYONLY)

# -----------------------------
# sm83_runner – SingleStepTests/sm83 CPU test harness
#
# Clone the test data once:
#   git clone --depth 1 https://github.com/SingleStepTests/sm83.git tests/sm83_data
#
# Then run:
#   ./build/Release/sm83_runner.exe tests/sm83_data/v1
# -----------------------------
add_executable(sm83_runner
  tests/sm83_runner.cpp
  src/core/MMU.cpp
  src/core/CPU.cpp
  src/core/Timer.cpp
  src/core/PPU.cpp
  src/core/OAMScan.cpp
  src/core/PixelFetcher.cpp
  src/core/Palettes.cpp
  src/core/opcodes/Opcodes.cpp
  src/core/opcodes/OpcodesCB.cpp
  src/cartridge/Cartridge.cpp
  src/cartridge/MBC0.cpp
  src/cartridge/MBC1.cpp
  src/cartridge/MBC2.cpp
)

target_include_directories(sm83_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(sm83_runner PRIVATE nlohmann_json::nlohmann_json)

if(MSVC)
  target_compile_options(sm83_runner PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
  target_compile_options(sm83_runner PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(sm83_runner PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# -----------------------------
# blargg_runner – headless Blargg ROM test harness
#
# Download ROMs from: https://gbdev.gg/wiki/articles/Test_ROMs
# Individual cpu_instrs sub-tests: 01-special.gb … 11-op a,(hl).gb
# Combined test: cpu_instrs.gb
#
# Usage: ./build/Release/blargg_runner.exe roms/01-special.gb
# -----------------------------
add_executable(blargg_runner
  tests/blargg_runner.cpp
  src/core/MMU.cpp
  src/core/CPU.cpp
  src/core/GameBoy.cpp
  src/core/Timer.cpp
  src/core/PPU.cpp
  src/core/OAMScan.cpp
  src/core/PixelFetcher.cpp
  src/core/Palettes.cpp
  src/core/opcodes/Opcodes.cpp
  src/core/opcodes/OpcodesCB.cpp
  src/cartridge/Cartridge.cpp
  src/cartridge/MBC0.cpp
  src/cartridge/MBC1.cpp
  src/cartridge/MBC2.cpp
)

target_include_directories(blargg_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(blargg_runner PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
  target_compile_options(blargg_runner PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(blargg_runner PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# -----------------------------
# mealybug_runner – PPU visual regression test harness
#
# ROMs + expected PNGs from:
#   https://github.com/mattcurrie/mealybug-tearoom-tests
# Clone into roms/mealybug-tearoom-tests/
#
# Usage (single test):
#   ./build/Release/mealybug_runner.exe roms/mealybug-tearoom-tests/ppu/m3_bgp_change.gb \
#       roms/mealybug-tearoom-tests/ppu/m3_bgp_change.dmg.png
#
# Batch (all DMG tests):
#   bash tests/run_mealybug.sh
#
# Requires ImageMagick (magick) on PATH for pixel comparison.
# -----------------------------
add_executable(mealybug_runner
  tests/mealybug_runner.cpp
  src/core/MMU.cpp
  src/core/CPU.cpp
  src/core/GameBoy.cpp
  src/core/Timer.cpp
  src/core/PPU.cpp
  src/core/OAMScan.cpp
  src/core/PixelFetcher.cpp
  src/core/Palettes.cpp
  src/core/opcodes/Opcodes.cpp
  src/core/opcodes/OpcodesCB.cpp
  src/cartridge/Cartridge.cpp
  src/cartridge/MBC0.cpp
  src/cartridge/MBC1.cpp
  src/cartridge/MBC2.cpp
)

target_include_directories(mealybug_runner PRIVATE  ${CMAKE_CURRENT_SOURCE_DIR})
# SYSTEM suppresses third-party warnings from stb headers
target_include_directories(mealybug_runner SYSTEM PRIVATE ${stb_SOURCE_DIR})

if(MSVC)
  target_compile_options(mealybug_runner PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
  target_compile_options(mealybug_runner PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(mealybug_runner PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# -----------------------------
# mooneye_runner – mooneye-test-suite register-based pass/fail harness
#
# Clone ROMs:
#   git clone https://github.com/Gekkio/mooneye-test-suite roms/mooneye-test-suite
#
# Usage (single test):
#   ./build/Release/mooneye_runner.exe roms/mooneye-test-suite/acceptance/add_sp_e_timing.gb
#
# Batch (all DMG-compatible tests):
#   bash tests/run_mooneye.sh
# -----------------------------
add_executable(mooneye_runner
  tests/mooneye_runner.cpp
  src/core/MMU.cpp
  src/core/CPU.cpp
  src/core/GameBoy.cpp
  src/core/Timer.cpp
  src/core/PPU.cpp
  src/core/OAMScan.cpp
  src/core/PixelFetcher.cpp
  src/core/Palettes.cpp
  src/core/opcodes/Opcodes.cpp
  src/core/opcodes/OpcodesCB.cpp
  src/cartridge/Cartridge.cpp
  src/cartridge/MBC0.cpp
  src/cartridge/MBC1.cpp
  src/cartridge/MBC2.cpp
)

target_include_directories(mooneye_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(mooneye_runner PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
  target_compile_options(mooneye_runner PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(mooneye_runner PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
