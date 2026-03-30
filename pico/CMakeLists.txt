cmake_minimum_required(VERSION 3.13)

# ---------------------------------------------------------------------------
# Pico SDK bootstrap - must happen before project()
#
# Set PICO_SDK_PATH one of two ways:
#   cmake -B build_pico2 -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_BOARD=pico2 pico/
#   export PICO_SDK_PATH=/path/to/pico-sdk && cmake -B build_pico2 -DPICO_BOARD=pico2 pico/
#
# Board selection:
#   Pico 2 (RP2350):  -DPICO_BOARD=pico2   (default)
#   Pico 1 (RP2040):  -DPICO_BOARD=pico
# ---------------------------------------------------------------------------
if(DEFINED ENV{PICO_SDK_PATH} AND NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()

if(NOT PICO_SDK_PATH)
    message(FATAL_ERROR
        "PICO_SDK_PATH is not set.\n"
        "Pass it with: cmake -DPICO_SDK_PATH=/path/to/pico-sdk ...")
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(SeaBoy_Pico C CXX ASM)

set(CMAKE_C_STANDARD   11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

pico_sdk_init()

# ---------------------------------------------------------------------------
# FatFs SD card library (carlk3/no-OS-FatFS-SD-SPI-RPi-Pico)
# The library lives in the FatFs_SPI/ subdirectory of the repo.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    FatFs_SPI
    GIT_REPOSITORY https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico.git
    GIT_TAG        master
)
FetchContent_GetProperties(FatFs_SPI)
if(NOT fatfs_spi_POPULATED)
    FetchContent_Populate(FatFs_SPI)
    add_subdirectory(${fatfs_spi_SOURCE_DIR}/FatFs_SPI ${fatfs_spi_BINARY_DIR})
endif()

# ---------------------------------------------------------------------------
# SeaBoy_pico target
# Includes all of src/core/ and src/cartridge/ but NO src/ui/ (desktop-only).
# ---------------------------------------------------------------------------
add_executable(SeaBoy_pico
    main_pico.cpp

    # Platform drivers
    input/Buttons.cpp
    display/ILI9225.cpp
    storage/SDCard.cpp
    storage/hw_config.c
    audio/I2SAudio.cpp

    # Core emulation
    ../src/core/MMU.cpp
    ../src/core/CPU.cpp
    ../src/core/GameBoy.cpp
    ../src/core/Joypad.cpp
    ../src/core/Timer.cpp
    ../src/core/PPU.cpp
    ../src/core/APU.cpp
    ../src/core/OAMScan.cpp
    ../src/core/PixelFetcher.cpp
    ../src/core/Palettes.cpp
    ../src/core/SaveState.cpp
    ../src/core/opcodes/Opcodes.cpp
    ../src/core/opcodes/OpcodesCB.cpp

    # Cartridge / MBC
    ../src/cartridge/Cartridge.cpp
    ../src/cartridge/MBC0.cpp
    ../src/cartridge/MBC1.cpp
    ../src/cartridge/MBC2.cpp
    ../src/cartridge/MBC3.cpp
    ../src/cartridge/MBC5.cpp
)

target_include_directories(SeaBoy_pico PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..   # repo root - lets #include "src/core/..." work
    ${CMAKE_CURRENT_SOURCE_DIR}      # pico/ dir - for future pico-local headers
)

# Generate C header from PIO assembly (i2s_out.pio -> i2s_out.pio.h)
pico_generate_pio_header(SeaBoy_pico ${CMAKE_CURRENT_SOURCE_DIR}/audio/i2s_out.pio)

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(SeaBoy_pico PRIVATE
    PICO_BUILD=1
)

# RP2040-specific flag (enables RGB565 framebuffer, 22050 Hz audio in later stages)
if(PICO_PLATFORM STREQUAL "rp2040")
    target_compile_definitions(SeaBoy_pico PRIVATE PICO_RP2040=1)
    message(STATUS "SeaBoy: RP2040 build (Pico 1) - RGB565 framebuffer, 22050 Hz audio")
else()
    message(STATUS "SeaBoy: RP2350 build (Pico 2) - RGBA8888 framebuffer, 48000 Hz audio")
endif()

# ---------------------------------------------------------------------------
# Pico SDK + FatFs library linkage
# ---------------------------------------------------------------------------
target_link_libraries(SeaBoy_pico PRIVATE
    pico_stdlib
    pico_multicore
    hardware_spi
    hardware_dma
    hardware_gpio
    hardware_pio
    hardware_pwm
    FatFs_SPI
)

# ---------------------------------------------------------------------------
# USB serial output (printf -> USB CDC; no physical UART needed)
# UART disabled to free GPIO 0/1 for other uses
# ---------------------------------------------------------------------------
pico_enable_stdio_usb(SeaBoy_pico  1)
pico_enable_stdio_uart(SeaBoy_pico 0)

# ---------------------------------------------------------------------------
# Compiler warnings (arm-none-eabi-gcc)
# ---------------------------------------------------------------------------
target_compile_options(SeaBoy_pico PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter   # common in stub/platform code
)

# ---------------------------------------------------------------------------
# Generate .uf2 (drag-and-drop flash), .bin, .hex, .map alongside the ELF
# ---------------------------------------------------------------------------
pico_add_extra_outputs(SeaBoy_pico)
