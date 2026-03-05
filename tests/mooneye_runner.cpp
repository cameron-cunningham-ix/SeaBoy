// mooneye_runner.cpp
// Automated test runner for mooneye-test-suite.
//
// Usage:
//   mooneye_runner <rom.gb> [--timeout <frames>]
//
// The runner executes the ROM until the LD B,B software breakpoint (opcode 0x40)
// is hit, then reads CPU registers B/C/D/E/H/L to determine pass or fail.
//
// Mooneye pass/fail protocol:
//   Pass: B=3  C=5  D=8  E=13 H=21 L=34  (Fibonacci sequence)
//   Fail: B=42 C=42 D=42 E=42 H=42 L=42
//
// Returns 0 on pass, 1 on fail/timeout.

#include "src/core/GameBoy.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

static constexpr uint8_t MOONEYE_PASS[6] = { 3, 5, 8, 13, 21, 34 };
static constexpr uint8_t MOONEYE_FAIL    = 0x42;

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: mooneye_runner <rom.gb> [--timeout <frames>]\n";
        return 1;
    }

    const std::string romPath = argv[1];

    uint64_t timeoutFrames = 120; // default: 2 seconds of GB time
    for (int i = 2; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--timeout") == 0)
            timeoutFrames = static_cast<uint64_t>(std::atoll(argv[i + 1]));
    }

    const uint64_t maxCycles = timeoutFrames * SeaBoy::TCYCLES_PER_FRAME;

    // --- Load ROM ---
    SeaBoy::GameBoy gb;
    if (!gb.loadROM(romPath))
    {
        std::cerr << "mooneye_runner: failed to load ROM: " << romPath << "\n";
        return 1;
    }

    // --- Tick loop: stop on LD B,B (0x40) breakpoint ---
    uint64_t elapsed       = 0;
    bool     breakpointHit = false;

    while (elapsed < maxCycles)
    {
        // Peek at current PC opcode without triggering side effects.
        // LD B,B (0x40) is the mooneye software breakpoint convention.
        if (gb.mmu().peek8(gb.cpu().registers().PC) == 0x40)
        {
            breakpointHit = true;
            break;
        }
        elapsed += gb.tick();
    }

    const std::string romName = std::filesystem::path(romPath).filename().string();
    const uint64_t    frames  = elapsed / SeaBoy::TCYCLES_PER_FRAME;

    if (!breakpointHit)
    {
        std::cerr << "mooneye_runner: TIMEOUT (" << timeoutFrames << " frames) - "
                  << romName << "\n";
        return 1;
    }

    // --- Read result registers ---
    const SeaBoy::Registers& regs = gb.cpu().registers();
    const uint8_t vals[6] = { regs.B, regs.C, regs.D, regs.E, regs.H, regs.L };

    // Check pass: Fibonacci 3/5/8/13/21/34
    bool passed = true;
    for (int i = 0; i < 6; ++i)
        if (vals[i] != MOONEYE_PASS[i]) { passed = false; break; }

    // Check known fail: all 0x42
    bool knownFail = true;
    for (int i = 0; i < 6; ++i)
        if (vals[i] != MOONEYE_FAIL) { knownFail = false; break; }

    std::cout << (passed ? "PASS" : "FAIL")
              << "  " << romName
              << "  (" << frames << " frame(s))";

    if (!passed)
    {
        // Print register dump for diagnosis
        std::cout << "  [B=" << static_cast<int>(regs.B)
                  << " C=" << static_cast<int>(regs.C)
                  << " D=" << static_cast<int>(regs.D)
                  << " E=" << static_cast<int>(regs.E)
                  << " H=" << static_cast<int>(regs.H)
                  << " L=" << static_cast<int>(regs.L)
                  << (knownFail ? " (fail)" : " (unknown)") << "]";
    }
    std::cout << "\n";

    return passed ? 0 : 1;
}
