![SeaBoy Logo](assets/SeaBoyLogo_icon.ico)
# SeaBoy

A Game Boy / Game Boy Color emulator written in C++20.

## Features

- Cycle-accurate SM83 CPU — passes all Blargg `cpu_instrs`, `instr_timing`, `mem_timing`, and `mem_timing-2` tests
- MBC0–5 cartridge support
- Timer with T-cycle accuracy
- PPU with OAM corruption bug emulation
- SDL3-based display and input
- Dear ImGui docking UI with integrated debugger
- Save states (slots 1–9)

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| CMake | 3.21 | Required for `TARGET_RUNTIME_DLLS` |
| Ninja | any | Recommended generator |
| C++ compiler | MSVC 2022 / GCC 12 / Clang 15 | C++20 required |
| Git | any | FetchContent clones dependencies |

All library dependencies (SDL3, Dear ImGui, Native File Dialog Extended) are fetched automatically by CMake at configure time — no manual installation needed.

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
├── CMakeLists.txt
├── fonts/
│   └── Roboto/           # Roboto-Regular font
├── src/
│   ├── core/             # CPU, MMU, PPU, APU, Timer, Joypad, GameBoy
│   ├── cartridge/        # Header parsing + MBC0–5
│   └── ui/               # UIPlatform, DebuggerUI, SettingsUI
├── tests/                # Unit tests
└── info/                 # Reference docs and Pan Docs snapshots
```

## Dependencies

| Library | Source | Purpose |
|---------|--------|---------|
| [SDL3](https://github.com/libsdl-org/SDL) | FetchContent | Window, renderer, input |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) | FetchContent | Debug UI |
| [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended) | FetchContent | ROM file picker |

## Reference Material

- [Pan Docs](https://gbdev.io/pandocs/) — primary hardware reference
- [GBZ80 instruction set](https://rgbds.gbdev.io/docs/v1.0.1/gbz80.7)
- [Opcode table](https://www.pastraiser.com/cpu/gameboy/gameboy_opcodes.html)
- [Another opcode table](https://izik1.github.io/gbops/index.html)
- [Blargg test ROMs](https://github.com/retrio/gb-test-roms)
- [mooneye-gb tests](https://github.com/Gekkio/mooneye-test-suite/)

## License

This project is licensed under the GNU General Public License v2.0. See the [LICENSE](LICENSE) file for details.
