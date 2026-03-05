// tests/blargg_runner.cpp
// Headless runner for Blargg Game Boy test ROMs.
//
// Downloads:
//   https://gbdev.gg/wiki/articles/Test_ROMs (Blargg section)
//   Individual cpu_instrs sub-tests: 01-special.gb - 11-op a,(hl).gb
//   Combined:                        cpu_instrs.gb
//
// Usage:
//   blargg_runner <rom.gb>
//   blargg_runner <rom.gb> --cycles 500000000
//
// Exit codes:
//   0 = "Passed" found in serial output
//   1 = "Failed" found, or timeout, or file error
//
// How it works:
//   The ROM runs headlessly through GameBoy::tick(), which drives CPU, Timer,
//   and PPU (and eventually APU) at M-cycle granularity via the bus callback.
//   Output is captured via the serial port (SB/SC at 0xFF01/02).
//   The PPU generates real VBlank/STAT interrupts so ROMs that poll LCD status
//   proceed normally without stubs.
//   The runner exits as soon as it sees "Passed" or "Failed" in the output.
//
// MBC support: MBC0 (no switching) and MBC1 (ROM bank switching).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "src/core/GameBoy.hpp"

// ---------------------------------------------------------------------------
// Completion detection
// ---------------------------------------------------------------------------
// Blargg ROMs report results via three possible channels:
//   1. Serial port (SB/SC at 0xFF01/02) - v1 test framework
//   2. Memory-mapped output in external RAM - v2 test framework
//      0xA001–0xA003: magic signature 0xDE, 0xB0, 0x61
//      0xA000:        status (0x80 = running, else done)
//      0xA004+:       null-terminated text output
//   3. LCD tile map (VRAM 0x9800–0x9BFF) - LCD-only ROMs (e.g. halt_bug.gb)
//      Tile indices correspond directly to ASCII values in Blargg's font.

static bool checkPassed(const std::string& s) { return s.find("Passed") != std::string::npos; }
static bool checkFailed(const std::string& s)
{
    return s.find("Failed") != std::string::npos || s.find("failed") != std::string::npos;
}
static bool isDone(const std::string& s) { return checkPassed(s) || checkFailed(s); }

// Check memory-mapped output for v2 test framework completion.
// Returns the text output if valid signature found and status != 0x80, else empty.
static std::string checkMemMappedOutput(const SeaBoy::MMU& mmu)
{
    // Check magic signature at 0xA001–0xA003
    if (mmu.peek8(0xA001) != 0xDE ||
        mmu.peek8(0xA002) != 0xB0 ||
        mmu.peek8(0xA003) != 0x61)
        return {};

    // Status 0x80 = still running
    if (mmu.peek8(0xA000) == 0x80)
        return {};

    // Read null-terminated text from 0xA004+
    std::string text;
    for (uint16_t addr = 0xA004; addr < 0xBFFF; ++addr)
    {
        uint8_t ch = mmu.peek8(addr);
        if (ch == 0) break;
        text += static_cast<char>(ch);
    }
    return text;
}

// Check the BG/Window tile maps for Blargg LCD-text output.
// Used for ROMs (e.g. halt_bug.gb) that display results on screen only and do
// not output via the serial port or the v2 memory-mapped framework.
// Blargg font: tile index == ASCII value for printable characters (0x20–0x7E).
// mmu.peek8() routes to PPU::peekVRAM() which bypasses Mode-3 locking.
static std::string checkTileMapOutput(const SeaBoy::MMU& mmu)
{
    static constexpr uint16_t MAPS[2]  = { 0x9800, 0x9C00 };
    static constexpr int      COLS     = 32;
    static constexpr int      VIS_COLS = 20;
    static constexpr int      VIS_ROWS = 18;

    for (uint16_t base : MAPS)
    {
        for (int row = 0; row < VIS_ROWS; ++row)
        {
            std::string line;
            for (int col = 0; col < VIS_COLS; ++col)
            {
                uint8_t tile = mmu.peek8(
                    static_cast<uint16_t>(base + row * COLS + col));
                line += (tile >= 0x20 && tile <= 0x7E)
                            ? static_cast<char>(tile)
                            : ' ';
            }
            if (line.find("Passed") != std::string::npos ||
                line.find("Failed") != std::string::npos)
            {
                // Collect all visible non-empty rows for display
                std::string full;
                for (int r = 0; r < VIS_ROWS; ++r)
                {
                    std::string rline;
                    for (int c = 0; c < VIS_COLS; ++c)
                    {
                        uint8_t t = mmu.peek8(
                            static_cast<uint16_t>(base + r * COLS + c));
                        rline += (t >= 0x20 && t <= 0x7E)
                                     ? static_cast<char>(t)
                                     : ' ';
                    }
                    auto end = rline.find_last_not_of(' ');
                    if (end != std::string::npos)
                        full += rline.substr(0, end + 1) + '\n';
                }
                return full.empty() ? line : full;
            }
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: blargg_runner <rom.gb> [--cycles <max>]\n\n"
                     "  Download ROMs from: https://gbdev.gg/wiki/articles/Test_ROMs\n"
                     "  Individual tests:   01-special.gb - 11-op a,(hl).gb\n"
                     "  Combined test:      cpu_instrs.gb\n";
        return 1;
    }

    const char* romPath   = argv[1];
    uint64_t    maxCycles = 500'000'000ULL; // ~120 s of Game Boy time

    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--cycles") == 0 && i + 1 < argc)
            maxCycles = std::stoull(argv[++i]);
    }

    // ---- set up emulator ----
    SeaBoy::GameBoy gameBoy;
    if (!gameBoy.loadROM(romPath))
    {
        std::fprintf(stderr, "Error: could not load '%s'\n", romPath);
        return 1;
    }

    // ---- print cartridge info (re-read header for display only) ----
    {
        std::ifstream f(romPath, std::ios::binary | std::ios::ate);
        auto sz = static_cast<size_t>(f.tellg());
        f.seekg(0);
        std::vector<uint8_t> hdr(std::min(sz, size_t(0x150)));
        f.read(reinterpret_cast<char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));

        const char* mbcNames[] = {"ROM only", "MBC1", "MBC1+RAM", "MBC1+RAM+BATTERY"};
        uint8_t mbcType = hdr.size() > 0x0147u ? hdr[0x0147] : 0;
        const char* mbcName = mbcType < 4 ? mbcNames[mbcType] : "unknown MBC";
        std::printf("ROM:  %s  (%zu KB, %s)\n", romPath, sz / 1024, mbcName);
    }
    std::printf("Max:  %.0f M cycles\n\n", static_cast<double>(maxCycles) / 1'000'000.0);

    // ---- run loop ----
    uint64_t    totalCycles = 0;
    std::string lastOutput;

    // Periodic threshold for alternative output channel checks (every 100 K cycles)
    uint64_t nextMemCheck = 100'000ULL;

    while (totalCycles < maxCycles)
    {
        totalCycles += gameBoy.tick();

        const std::string& serial = gameBoy.serialOutput();

        if (serial.size() != lastOutput.size())
        {
            // Stream new characters to stdout as they arrive
            for (size_t i = lastOutput.size(); i < serial.size(); ++i)
                std::putchar(serial[i]);
            std::fflush(stdout);
            lastOutput = serial;

            if (isDone(serial))
                break;
        }

        // Periodically check alternative output channels (every 100 K cycles)
        if (totalCycles >= nextMemCheck)
        {
            // v2 test framework: memory-mapped output at 0xA000+
            std::string memOut = checkMemMappedOutput(gameBoy.mmu());
            if (!memOut.empty())
            {
                std::printf("%s", memOut.c_str());
                if (memOut.back() != '\n')
                    std::putchar('\n');
                std::fflush(stdout);
                lastOutput = memOut;
                break;
            }

            // LCD-only ROMs (e.g. halt_bug.gb): result written to VRAM tile map
            std::string tileOut = checkTileMapOutput(gameBoy.mmu());
            if (!tileOut.empty())
            {
                std::printf("%s", tileOut.c_str());
                if (tileOut.back() != '\n')
                    std::putchar('\n');
                std::fflush(stdout);
                lastOutput = tileOut;
                break;
            }

            nextMemCheck += 100'000ULL;
        }
    }

    // Ensure final newline
    if (!lastOutput.empty() && lastOutput.back() != '\n')
        std::putchar('\n');

    // ---- report ----
    std::printf("\nCycles: %llu (%.2f s Game Boy time)\n",
                static_cast<unsigned long long>(totalCycles),
                static_cast<double>(totalCycles) / 4'194'304.0);

    // Check serial output first, then memory-mapped output
    const std::string& serial = gameBoy.serialOutput();
    std::string result = serial.empty() ? lastOutput : serial;
    if (checkPassed(result))
    {
        std::puts("Result: PASSED");
        return 0;
    }
    if (checkFailed(result))
    {
        std::puts("Result: FAILED");
        return 1;
    }
    std::printf("Result: TIMEOUT (no conclusion after %llu cycles)\n",
                static_cast<unsigned long long>(maxCycles));
    return 1;
}
