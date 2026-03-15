// tests/microtest_runner.cpp
// Headless runner for gbmicrotest ROM suite.
//
// gbmicrotest: https://github.com/aappleby/gbmicrotest
// Each test checks a single register/memory value at a precise cycle and
// reports pass/fail via HRAM:
//   0xFF80 - actual result
//   0xFF81 - expected result
//   0xFF82 - 0x01 = PASS, 0xFF = FAIL (0x00 while still running)
//
// Usage:
//   microtest_runner <rom.gb> [--timeout <frames>]
//
// Exit codes:
//   0 = PASS (0xFF82 == 0x01)
//   1 = FAIL, timeout, or file error

#include "src/core/GameBoy.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

static constexpr uint16_t ADDR_ACTUAL   = 0xFF80;
static constexpr uint16_t ADDR_EXPECTED = 0xFF81;
static constexpr uint16_t ADDR_RESULT   = 0xFF82;

static constexpr uint8_t RESULT_PASS = 0x01;
static constexpr uint8_t RESULT_FAIL = 0xFF;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: microtest_runner <rom.gb> [--timeout <frames>]\n";
        return 1;
    }

    const std::string romPath = argv[1];

    uint64_t timeoutFrames = 100;
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
        std::cerr << "microtest_runner: failed to load ROM: " << romPath << "\n";
        return 1;
    }

    // --- Tick loop: poll 0xFF82 for pass/fail ---
    uint64_t elapsed = 0;
    uint8_t  result  = 0x00;

    while (elapsed < maxCycles)
    {
        elapsed += gb.tick();

        result = gb.mmu().peek8(ADDR_RESULT);
        if (result == RESULT_PASS || result == RESULT_FAIL)
            break;
    }

    const std::string romName = std::filesystem::path(romPath).filename().string();
    const uint64_t    frames  = elapsed / SeaBoy::TCYCLES_PER_FRAME;

    if (result == RESULT_PASS)
    {
        std::cout << "PASS  " << romName << "  (" << frames << " frame(s))\n";
        return 0;
    }

    if (result == RESULT_FAIL)
    {
        uint8_t actual   = gb.mmu().peek8(ADDR_ACTUAL);
        uint8_t expected = gb.mmu().peek8(ADDR_EXPECTED);
        std::cout << "FAIL  " << romName << "  (" << frames << " frame(s))"
                  << "  [actual=0x" << std::hex << static_cast<int>(actual)
                  << " expected=0x" << static_cast<int>(expected) << "]\n";
        return 1;
    }

    std::cout << "TIMEOUT  " << romName << "  (" << timeoutFrames << " frame(s))\n";
    return 1;
}
