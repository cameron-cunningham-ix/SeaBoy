#pragma once

#include <cstdint>

// ILI9225 176×220 SPI LCD driver for SeaBoy.
//
// Display geometry (landscape mode - rotated 90 degrees left):
//   Physical panel: 176×220 portrait.
//   With entry mode rotation: logical 220 wide × 176 tall.
//   GB 160×144 drawn 1:1, centered in 220×176.
//   Horizontal border: (220 - 160) / 2 = 30 px each side.
//   Vertical border:   (176 - 144) / 2 = 16 px top and bottom.
//
// SPI0 wiring:
//   GPIO 17 -> CS   (software chip-select)
//   GPIO 18 -> SCK  (SPI0 SCK)
//   GPIO 19 -> SDI  (SPI0 TX / MOSI)
//   GPIO 20 -> RS   (data/command, active-high = data)
//   GPIO 21 -> RST  (active-low reset)
//   GPIO 22 -> LED  (backlight - driven high or PWM)

class ILI9225
{
public:
    // Physical panel dimensions
    static constexpr unsigned int kPanelW  = 176;
    static constexpr unsigned int kPanelH  = 220;

    // Logical dimensions after landscape rotation
    static constexpr unsigned int kScreenW = 220;
    static constexpr unsigned int kScreenH = 176;

    static constexpr unsigned int kGBW     = 160;
    static constexpr unsigned int kGBH     = 144;
    static constexpr unsigned int kOffsetX = (kScreenW - kGBW) / 2;   // 30
    static constexpr unsigned int kOffsetY = (kScreenH - kGBH) / 2;   // 16

    // Initialise SPI0, reset the display, send power-on sequence,
    // clear the screen to black, and turn on the backlight.
    void init();

#ifdef PICO_RP2040
    // Blit 160×144 RGB565 framebuffer to the LCD (no conversion needed).
    void drawFrame(const uint16_t* rgb565);
#else
    // Blit 160×144 RGBA8888 framebuffer to the LCD (converts to RGB565 on the fly).
    void drawFrame(const uint32_t* rgba);
#endif

    // Fill the entire display with a single RGB565 color (test pattern helper).
    void fillScreen(uint16_t rgb565);

    // Send a full 220×176 RGB565 buffer to the display (used by ROM menu).
    void fillScreenBuffer(const uint16_t* rgb565);

private:
    // GPIO pin assignments
    static constexpr unsigned int kPinCS  = 17;
    static constexpr unsigned int kPinSCK = 18;
    static constexpr unsigned int kPinSDI = 19;
    static constexpr unsigned int kPinRS  = 20;
    static constexpr unsigned int kPinRST = 21;
    static constexpr unsigned int kPinLED = 22;

    // Low-level helpers
    void hardwareReset();
    void writeReg(uint16_t reg, uint16_t val);
    void writeIndex(uint16_t reg);
    void writeData16(uint16_t val);
    void setWindow(unsigned int x0, unsigned int y0, unsigned int x1, unsigned int y1);
    void beginPixelWrite();
    void clearBorders();

    // DMA ping-pong line buffers (160 RGB565 pixels = 320 bytes each)
    uint16_t m_lineBuf[2][kGBW]{};
    unsigned int m_activeBuf = 0;
    int m_dmaChannel = -1;

    void waitDma();
    void launchDma(const uint16_t* buf, unsigned int count);
};
