# SeaBoy

A Game Boy / Game Boy Color emulator written in C++20.

> **Status:** Early development - UI shell is functional; core emulation (MMU, CPU, PPU, APU) is in progress.

## Features (planned)

- Game Boy (DMG) and Game Boy Color (CGB) support
- Cycle-accurate CPU (SM83 / LR35902)
- MBC0–5 cartridge support
- SDL3-based display and input
- Dear ImGui docking UI with integrated debugger

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| CMake | 3.21 | Required for `TARGET_RUNTIME_DLLS` |
| Ninja | any | Recommended generator |
| C++ compiler | MSVC 2022 / GCC 12 / Clang 15 | C++20 required |
| Git | any | FetchContent clones dependencies |

All library dependencies (SDL3, Dear ImGui, Native File Dialog Extended) are fetched automatically by CMake at configure time - no manual installation needed.

## Building

```bash
# 1. Clone the repo
git clone https://github.com/cameron-cunningham-ix/SeaBoy.git
cd SeaBoy

# 2. Configure (defaults to Release)
cmake -B build -G Ninja

# 3. Compile
cmake --build build

# 4. Run
./build/Release/SeaBoy.exe   # Windows
./build/Release/SeaBoy       # Linux / macOS
```

### Debug build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/Debug/SeaBoy.exe
```

### Visual Studio (multi-config)

```bash
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## Project Structure

```
SeaBoy/
├── main.cpp              # Entry point
├── UIPlatform.h          # SDL3 + ImGui window, renderer, input
├── CMakeLists.txt
├── fonts/
│   └── Roboto/           # Roboto-Regular font
├── src/
│   ├── core/             # CPU, MMU, PPU, APU, Timer, Joypad, GameBoy
│   ├── cartridge/        # Header parsing + MBC0–5
│   ├── ui/               # DebuggerUI, SettingsUI
│   └── util/             # Bits.h, Logger.h
├── tests/                # Unit tests
├── specs/                # Markdown spec files per component
├── roms/                 # ROMs (gitignored)
└── info/                 # Reference docs and Pan Docs snapshots
```

## Dependencies

| Library | Source | Purpose |
|---------|--------|---------|
| [SDL3](https://github.com/libsdl-org/SDL) | FetchContent | Window, renderer, input |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) | FetchContent | Debug UI |
| [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) | FetchContent | ROM file picker |

## Reference Material

- [Pan Docs](https://gbdev.io/pandocs/) - primary hardware reference
- [GBZ80 instruction set](https://rgbds.gbdev.io/docs/v1.0.1/gbz80.7)
- [Opcode table](https://www.pastraiser.com/cpu/gameboy/gameboy_opcodes.html)
- [Blargg test ROMs](https://gbdev.gg/wiki/articles/Test_ROMs)
- [mooneye-gb tests](https://github.com/Gekkio/mooneye-gb)

## License

TBD
