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
# Executable
# -----------------------------
add_executable(SeaBoy
  main.cpp
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
