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
//   The ROM runs headlessly through GameBoy::tick(), which drives CPU + Timer
//   (and eventually PPU/APU).  Output is captured via the serial port (SB/SC at
//   0xFF01/02).  LCD stub registers prevent the ROM from spinning on VBlank.
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
// Blargg ROMs report results via two possible channels:
//   1. Serial port (SB/SC at 0xFF01/02) - v1 test framework
//   2. Memory-mapped output in external RAM - v2 test framework
//      0xA001–0xA003: magic signature 0xDE, 0xB0, 0x61
//      0xA000:        status (0x80 = running, else done)
//      0xA004+:       null-terminated text output

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

    // Check memory-mapped output every 100K cycles (v2 framework detection)
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

        // Check memory-mapped output periodically (v2 test framework)
        if (totalCycles >= nextMemCheck)
        {
            std::string memOut = checkMemMappedOutput(gameBoy.mmu());
            if (!memOut.empty())
            {
                std::printf("%s", memOut.c_str());
                if (!memOut.empty() && memOut.back() != '\n')
                    std::putchar('\n');
                std::fflush(stdout);
                // Use memory-mapped output as the serial output for result checking
                lastOutput = memOut;
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
