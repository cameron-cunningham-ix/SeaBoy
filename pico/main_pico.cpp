#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sem.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

#include "audio/I2SAudio.hpp"
#include "display/ILI9225.hpp"
#include "input/Buttons.hpp"
#include "storage/SDCard.hpp"
#include "src/core/GameBoy.hpp"
#include "src/core/APU.hpp"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

// ---------------------------------------------------------------------------
// Stage 6 - Dual-core emulation loop
//
// Core 0: emulation (GameBoy::tick), button polling, audio pump
// Core 1: LCD rendering (ILI9225::drawFrame via SPI DMA)
//
// Frame handoff via semaphores:
//   Core 0 copies PPU framebuffer -> s_frameBuf, signals s_frameReady
//   Core 1 waits for s_frameReady, renders, signals s_frameConsumed
//   Core 0 waits for s_frameConsumed before next copy (overlap: emulation
//   runs in parallel with LCD SPI transfer)
// ---------------------------------------------------------------------------

// GameBoy as a static global (BSS segment) - too large (~150 KB) for the stack.
static SeaBoy::GameBoy s_gameBoy;

// Platform drivers (static to keep off the stack)
static ILI9225   s_display;
static Buttons   s_buttons;
static SDCard    s_sdcard;
static I2SAudio  s_audio;

// ROM file list
static RomEntry s_romList[kMaxRomEntries];
static unsigned int s_romCount = 0;

// ---------------------------------------------------------------------------
// Dual-core frame handoff
// ---------------------------------------------------------------------------

// Shared frame buffer: Core 0 writes, Core 1 reads.
#if defined(PICO_RP2040)
static uint16_t s_frameBuf[160 * 144]; // RGB565 - 45 KB
#else
static uint32_t s_frameBuf[160 * 144]; // RGBA8888 - 90 KB
#endif

static semaphore_t s_frameReady;    // Core 0 -> Core 1: new frame available
static semaphore_t s_frameConsumed; // Core 1 -> Core 0: done reading s_frameBuf

// Core 1 entry point: waits for frames and pushes them to the LCD.
static void displayThread()
{
    while (true)
    {
        sem_acquire_blocking(&s_frameReady);
        s_display.drawFrame(s_frameBuf);
        sem_release(&s_frameConsumed);
    }
}

// ---------------------------------------------------------------------------
// Simple 5×7 bitmap font for the ROM menu (ASCII 32-126, stored column-major)
// Each char is 5 columns × 7 rows. Bit 0 = top row.
// ---------------------------------------------------------------------------
static constexpr uint8_t kFont5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x08,0x2A,0x1C,0x2A,0x08}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x00,0x08,0x14,0x22,0x41}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x41,0x22,0x14,0x08,0x00}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x01,0x01}, // 'F'
    {0x3E,0x41,0x41,0x51,0x32}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x04,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x7F,0x20,0x18,0x20,0x7F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x03,0x04,0x78,0x04,0x03}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x00,0x7F,0x41,0x41}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\'
    {0x41,0x41,0x7F,0x00,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x08,0x14,0x54,0x54,0x3C}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x00,0x7F,0x10,0x28,0x44}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x08,0x08,0x2A,0x1C,0x08}, // '~'
};

// Draw a single char at pixel (px, py) in the given RGB565 color.
// Writes directly into a 220×176 framebuffer (landscape menu, full-screen).
static void drawChar(uint16_t* fb, unsigned int px, unsigned int py,
                     char ch, uint16_t color)
{
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* glyph = kFont5x7[ch - 32];
    for (unsigned int col = 0; col < 5; ++col)
    {
        uint8_t bits = glyph[col];
        for (unsigned int row = 0; row < 7; ++row)
        {
            if (bits & (1u << row))
            {
                unsigned int x = px + col;
                unsigned int y = py + row;
                if (x < ILI9225::kScreenW && y < ILI9225::kScreenH)
                    fb[y * ILI9225::kScreenW + x] = color;
            }
        }
    }
}

// Draw a string at pixel (px, py). 6 px per char (5 + 1 gap).
static void drawString(uint16_t* fb, unsigned int px, unsigned int py,
                       const char* str, uint16_t color)
{
    while (*str)
    {
        drawChar(fb, px, py, *str, color);
        px += 6;
        ++str;
    }
}

// ---------------------------------------------------------------------------
// ROM selection menu - renders to a 176×220 RGB565 buffer and sends to LCD
// ---------------------------------------------------------------------------
static int showRomMenu()
{
    if (s_romCount == 0) return -1;

    // Menu framebuffer - 220×176 RGB565 (77 KB, allocated on heap)
    constexpr unsigned int kMenuW = ILI9225::kScreenW;  // 220
    constexpr unsigned int kMenuH = ILI9225::kScreenH;  // 176
    uint16_t* menuFb = new(std::nothrow) uint16_t[kMenuW * kMenuH];
    if (!menuFb)
    {
        printf("Menu: failed to allocate framebuffer\n");
        return -1;
    }

    int selected = 0;
    int scrollTop = 0;
    constexpr int kVisibleRows = 16;  // 176 / 10 = 17 rows, -1 for title
    constexpr int kRowHeight   = 10;
    constexpr uint16_t kWhite  = 0xFFFF;
    constexpr uint16_t kYellow = 0xFFE0;
    constexpr uint16_t kGray   = 0x7BEF;

    bool needsRedraw = true;

    while (true)
    {
        // Poll buttons
        s_buttons.poll(s_gameBoy);

        // Check for directional input (reading raw GPIO for simplicity)
        // We reuse the change callback approach but for the menu we just
        // check the current button state after polling.
        // The callback will have already called setButton on s_gameBoy,
        // but we need menu navigation. Use a simple approach: check edges.

        // We'll poll manually for menu navigation using settingsPressed
        // and a simple state approach.

        // Actually, let's just poll the GPIO directly for responsiveness.
        uint32_t gpioAll = gpio_get_all();
        // GPIO mapping: 2=Up, 3=Down, 4=Left, 5=Right, 6=A, 7=B
        // Active low: pressed when bit is 0
        static uint32_t prevGpio = 0xFFFFFFFF;
        uint32_t changed = gpioAll ^ prevGpio;
        uint32_t pressed = changed & ~gpioAll; // newly pressed (went low)
        prevGpio = gpioAll;

        if (pressed & (1u << 2)) { selected--; needsRedraw = true; } // Up
        if (pressed & (1u << 3)) { selected++; needsRedraw = true; } // Down
        if (pressed & (1u << 6)) { delete[] menuFb; return selected; } // A = select
        if (pressed & (1u << 7)) { delete[] menuFb; return -1; }      // B = cancel

        // Clamp selection
        if (selected < 0) selected = 0;
        if (selected >= static_cast<int>(s_romCount))
            selected = static_cast<int>(s_romCount) - 1;

        // Auto-scroll
        if (selected < scrollTop) scrollTop = selected;
        if (selected >= scrollTop + kVisibleRows)
            scrollTop = selected - kVisibleRows + 1;

        if (needsRedraw)
        {
            // Clear to black
            std::memset(menuFb, 0, kMenuW * kMenuH * 2);

            // Title
            drawString(menuFb, 2, 2, "SeaBoy - Select ROM", kYellow);

            // ROM list
            int endIdx = scrollTop + kVisibleRows;
            if (endIdx > static_cast<int>(s_romCount))
                endIdx = static_cast<int>(s_romCount);

            for (int i = scrollTop; i < endIdx; ++i)
            {
                unsigned int py = static_cast<unsigned int>((i - scrollTop + 1) * kRowHeight + 4);
                uint16_t color = (i == selected) ? kYellow : kWhite;

                // Highlight bar for selected item
                if (i == selected)
                {
                    for (unsigned int y = py; y < py + 9 && y < kMenuH; ++y)
                        for (unsigned int x = 0; x < kMenuW; ++x)
                            menuFb[y * kMenuW + x] = 0x0010; // dark blue
                }

                // Truncate filename to fit 220px / 6px = 36 chars
                char truncName[37];
                std::strncpy(truncName, s_romList[i].name, 36);
                truncName[36] = '\0';

                drawString(menuFb, 2, py + 1, truncName, color);
            }

            // Scroll indicators
            if (scrollTop > 0)
                drawString(menuFb, 208, 14, "^", kGray);
            if (endIdx < static_cast<int>(s_romCount))
                drawString(menuFb, 208, kMenuH - 10, "v", kGray);

            // Send menu framebuffer to LCD (full screen)
            s_display.fillScreenBuffer(menuFb);

            needsRedraw = false;
        }

        sleep_ms(30); // ~33 Hz menu refresh
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main()
{
    set_sys_clock_khz(250000, true);
    stdio_init_all();
    sleep_ms(2000); // let USB CDC enumerate on the host

#if defined(PICO_RP2040)
    printf("SeaBoy Pico1 RP2040 boot OK\n");
#else
    printf("SeaBoy Pico2 RP2350 boot OK\n");
#endif
    printf("sys_clk = %lu kHz\n", clock_get_hz(clk_sys) / 1000);

    // ---- Display init -------------------------------------------------------
    printf("ILI9225: initialising...\n");
    s_display.init();

    // ---- Buttons init -------------------------------------------------------
    s_buttons.init();
    printf("Buttons: init OK\n");

    // ---- SD card init -------------------------------------------------------
    printf("SD: mounting...\n");
    if (!s_sdcard.mount())
    {
        printf("SD: mount FAILED - halting\n");
        s_display.fillScreen(0xF800); // red = error
        while (true) tight_loop_contents();
    }

    // ---- List ROMs ----------------------------------------------------------
    s_romCount = s_sdcard.listROMs(s_romList, kMaxRomEntries);
    if (s_romCount == 0)
    {
        printf("SD: no .gb/.gbc files found - halting\n");
        s_display.fillScreen(0xF800);
        while (true) tight_loop_contents();
    }

    // ---- ROM selection menu -------------------------------------------------
    int choice = showRomMenu();
    if (choice < 0)
    {
        printf("Menu: cancelled - halting\n");
        s_display.fillScreen(0x0000);
        while (true) tight_loop_contents();
    }

    printf("Menu: selected '%s' (%u bytes)\n",
           s_romList[choice].name, s_romList[choice].size);

    // ---- Load ROM -----------------------------------------------------------
    // Read the ROM file directly into a std::vector, then move it into the
    // Cartridge. This avoids a double allocation (raw buffer + Cartridge copy)
    // which would exhaust SRAM on RP2040.
    uint32_t romSize = s_romList[choice].size;

    char romPath[80];
    snprintf(romPath, sizeof(romPath), "0:/%s", s_romList[choice].name);

    std::vector<uint8_t> romData;
    romData.resize(romSize);

    uint32_t bytesRead = s_sdcard.readFile(romPath, romData.data(), romSize);
    if (bytesRead == 0)
    {
        printf("ROM: read failed - halting\n");
        s_display.fillScreen(0xF800);
        while (true) tight_loop_contents();
    }
    romData.resize(bytesRead); // trim to actual bytes read

    printf("ROM: read %lu bytes\n", static_cast<unsigned long>(bytesRead));

    if (!s_gameBoy.loadROM(std::move(romData)))
    {
        printf("ROM: GameBoy::loadROM failed - halting\n");
        s_display.fillScreen(0xF800);
        while (true) tight_loop_contents();
    }

    // romData has been moved into the Cartridge - no separate free needed.
    printf("ROM: loaded OK, CGB=%d\n", s_gameBoy.isCGB());

    // ---- Audio init ---------------------------------------------------------
#if defined(PICO_RP2040)
    s_audio.init(22050);
#else
    s_audio.init(48000);
#endif
    s_audio.setAPU(&s_gameBoy.apu());
    s_audio.start();

    // Clear display borders for game rendering
    s_display.fillScreen(0x0000);

    // ---- Launch Core 1 (display thread) -------------------------------------
    sem_init(&s_frameReady, 0, 1);    // starts empty: no frame available yet
    sem_init(&s_frameConsumed, 1, 1); // starts released: Core 0 can write immediately

    multicore_launch_core1(displayThread);
    printf("Core 1: display thread launched\n");

    // ---- Emulation loop (Core 0) --------------------------------------------
    printf("Emulation: starting\n");

    while (true)
    {
        uint64_t frameStart = time_us_64();

        // Poll buttons
        s_buttons.poll(s_gameBoy);
        if (s_buttons.settingsPressed())
        {
            s_buttons.clearSettings();
            // TODO Stage 7: in-game menu (save/load/reset)
        }

        // Run one frame of emulation
        uint32_t tCycles = 0;
        while (tCycles < SeaBoy::TCYCLES_PER_FRAME)
            tCycles += s_gameBoy.tick();

        // Refill any completed audio DMA buffers from APU
        s_audio.pump();

        // Hand frame to Core 1 for LCD rendering.
        // Wait for Core 1 to finish with the previous frame first.
        sem_acquire_blocking(&s_frameConsumed);
        std::memcpy(s_frameBuf, s_gameBoy.getFrameBuffer(), sizeof(s_frameBuf));
        sem_release(&s_frameReady);

        // Frame pace: spin until 16,742 us elapsed (~59.73 Hz).
        // Core 1 renders the LCD in parallel via SPI DMA while we wait.
        // Keep pumping audio during the wait to minimize underruns.
        while (time_us_64() - frameStart < 16742)
        {
            s_audio.pump();
            tight_loop_contents();
        }
    }
}
