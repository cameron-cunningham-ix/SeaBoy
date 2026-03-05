// mealybug_runner.cpp
// Automated PPU visual regression test runner for mealybug-tearoom-tests.
//
// Usage:
//   mealybug_runner <rom.gb> <expected.png> [--timeout <frames>]
//
// The runner executes the ROM until the LD B,B software breakpoint (opcode 0x40)
// is hit, then saves a grayscale PNG of the PPU framebuffer and compares it to
// the expected image using ImageMagick.
//
// Returns 0 on pixel-perfect match, 1 on any failure.
//
// DMG grayscale mapping (mealybug requirement):
//   PPU shade 0 (white)      -> 0xFF
//   PPU shade 1 (light gray) -> 0xAA
//   PPU shade 2 (dark gray)  -> 0x55
//   PPU shade 3 (black)      -> 0x00
// These are already the values stored in the RGBA8888 framebuffer R channel.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "src/core/GameBoy.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#  define POPEN  _popen
#  define PCLOSE _pclose
#else
#  define POPEN  popen
#  define PCLOSE pclose
#endif

// ---------------------------------------------------------------------------
// Run ImageMagick pixel comparison.
// Returns the number of differing pixels, or -1 on error (magick not found).
// ---------------------------------------------------------------------------
static int compareImages(const std::string& screenshot, const std::string& expected)
{
    // magick compare -metric AE a.png b.png NULL: 2>&1
    // Outputs the absolute-error pixel count to stderr (redirected to stdout).
    std::string cmd = "magick compare -metric AE \""
                    + screenshot + "\" \""
                    + expected   + "\" NULL: 2>&1";

#ifdef _WIN32
    FILE* pipe = POPEN(cmd.c_str(), "rb");
#else
    FILE* pipe = POPEN(cmd.c_str(), "r");
#endif
    if (!pipe)
        return -1;

    char buf[128] = {};
    if (!fgets(buf, sizeof(buf), pipe))
    {
        PCLOSE(pipe);
        return -1;
    }
    PCLOSE(pipe);

    // Output is a number (possibly float like "0" or "1234").
    // atoi handles both.
    return std::atoi(buf);
}

// ---------------------------------------------------------------------------
// Save the 160×144 RGBA8888 framebuffer as an 8-bit grayscale PNG.
// The R channel of each pixel already holds the canonical DMG shade value.
// ---------------------------------------------------------------------------
static bool saveGrayscalePNG(const std::string& path, const uint32_t* framebuffer)
{
    constexpr int W = 160;
    constexpr int H = 144;
    uint8_t gray[W * H];
    for (int i = 0; i < W * H; ++i)
        gray[i] = static_cast<uint8_t>((framebuffer[i] >> 24) & 0xFF); // R channel

    return stbi_write_png(path.c_str(), W, H, 1, gray, W) != 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv)
{
    // --- Argument parsing ---
    if (argc < 3)
    {
        std::cerr << "Usage: mealybug_runner <rom.gb> <expected.png> [--timeout <frames>]\n";
        return 1;
    }

    const std::string romPath      = argv[1];
    const std::string expectedPath = argv[2];

    uint64_t timeoutFrames = 120; // default: 2 seconds of GB time
    for (int i = 3; i < argc - 1; ++i)
    {
        if (std::strcmp(argv[i], "--timeout") == 0)
            timeoutFrames = static_cast<uint64_t>(std::atoll(argv[i + 1]));
    }

    const uint64_t maxCycles = timeoutFrames * SeaBoy::TCYCLES_PER_FRAME;

    // --- Load ROM ---
    SeaBoy::GameBoy gb;
    if (!gb.loadROM(romPath))
    {
        std::cerr << "mealybug_runner: failed to load ROM: " << romPath << "\n";
        return 1;
    }

    // --- Tick loop: stop on LD B,B (0x40) breakpoint ---
    uint64_t elapsed      = 0;
    bool     breakpointHit = false;

    while (elapsed < maxCycles)
    {
        // Peek at current PC opcode without triggering side effects.
        // LD B,B (0x40) is the mealybug software breakpoint convention.
        if (gb.mmu().peek8(gb.cpu().registers().PC) == 0x40)
        {
            breakpointHit = true;
            break;
        }
        elapsed += gb.tick();
    }

    if (!breakpointHit)
    {
        std::cerr << "mealybug_runner: TIMEOUT (" << timeoutFrames << " frames) - "
                  << romPath << "\n";
        return 1;
    }

    // --- Save screenshot ---
    // Place screenshot alongside the ROM so the path is deterministic.
    const std::string screenshotPath =
        std::filesystem::path(romPath).replace_extension(".screenshot.png").string();

    if (!saveGrayscalePNG(screenshotPath, gb.getFrameBuffer()))
    {
        std::cerr << "mealybug_runner: failed to write PNG: " << screenshotPath << "\n";
        return 1;
    }

    // --- Compare with ImageMagick ---
    const int pixelDiff = compareImages(screenshotPath, expectedPath);

    if (pixelDiff < 0)
    {
        std::remove(screenshotPath.c_str());
        std::cerr << "mealybug_runner: ImageMagick comparison failed "
                     "(is 'magick' on PATH?) - " << romPath << "\n";
        return 1;
    }

    const bool passed = (pixelDiff == 0);
    if (passed)
        std::remove(screenshotPath.c_str()); // clean up on pass; keep on fail for inspection

    std::cout << (passed ? "PASS" : "FAIL")
              << "  " << std::filesystem::path(romPath).filename().string()
              << "  (" << pixelDiff << " pixel(s) differ)";
    if (!passed)
        std::cout << "  [screenshot: " << screenshotPath << "]";
    std::cout << "\n";

    return passed ? 0 : 1;
}
