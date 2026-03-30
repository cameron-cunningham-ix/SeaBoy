#include "display/ILI9225.hpp"

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// ILI9225 register addresses (subset used by this driver)
// ---------------------------------------------------------------------------
static constexpr uint16_t REG_DRIVER_OUTPUT     = 0x01;
static constexpr uint16_t REG_LCD_AC_DRIVING    = 0x02;
static constexpr uint16_t REG_ENTRY_MODE        = 0x03;
static constexpr uint16_t REG_DISPLAY_CTRL1     = 0x07;
static constexpr uint16_t REG_BLANK_PERIOD      = 0x08;
static constexpr uint16_t REG_FRAME_CYCLE       = 0x0B;
static constexpr uint16_t REG_INTERFACE_CTRL    = 0x0C;
static constexpr uint16_t REG_OSC_CTRL          = 0x0F;
static constexpr uint16_t REG_POWER_CTRL1       = 0x10;
static constexpr uint16_t REG_POWER_CTRL2       = 0x11;
static constexpr uint16_t REG_POWER_CTRL3       = 0x12;
static constexpr uint16_t REG_POWER_CTRL4       = 0x13;
static constexpr uint16_t REG_POWER_CTRL5       = 0x14;
static constexpr uint16_t REG_VCI_RECYCLING     = 0x15;
static constexpr uint16_t REG_RAM_ADDR_SET1     = 0x20;  // GRAM horizontal address
static constexpr uint16_t REG_RAM_ADDR_SET2     = 0x21;  // GRAM vertical address
static constexpr uint16_t REG_GRAM_DATA         = 0x22;  // GRAM read/write data
static constexpr uint16_t REG_GATE_SCAN_CTRL    = 0x30;
static constexpr uint16_t REG_VERT_SCROLL1      = 0x31;
static constexpr uint16_t REG_VERT_SCROLL2      = 0x32;
static constexpr uint16_t REG_VERT_SCROLL3      = 0x33;
static constexpr uint16_t REG_PARTIAL_DRIVING1  = 0x34;
static constexpr uint16_t REG_PARTIAL_DRIVING2  = 0x35;
static constexpr uint16_t REG_HORIZ_WIN_ADDR1   = 0x36;  // column end
static constexpr uint16_t REG_HORIZ_WIN_ADDR2   = 0x37;  // column start
static constexpr uint16_t REG_VERT_WIN_ADDR1    = 0x38;  // row end
static constexpr uint16_t REG_VERT_WIN_ADDR2    = 0x39;  // row start
static constexpr uint16_t REG_GAMMA_CTRL1       = 0x50;
static constexpr uint16_t REG_GAMMA_CTRL2       = 0x51;
static constexpr uint16_t REG_GAMMA_CTRL3       = 0x52;
static constexpr uint16_t REG_GAMMA_CTRL4       = 0x53;
static constexpr uint16_t REG_GAMMA_CTRL5       = 0x54;
static constexpr uint16_t REG_GAMMA_CTRL6       = 0x55;
static constexpr uint16_t REG_GAMMA_CTRL7       = 0x56;
static constexpr uint16_t REG_GAMMA_CTRL8       = 0x57;
static constexpr uint16_t REG_GAMMA_CTRL9       = 0x58;
static constexpr uint16_t REG_GAMMA_CTRL10      = 0x59;

// ---------------------------------------------------------------------------
// SPI instance
// ---------------------------------------------------------------------------
#define LCD_SPI spi0

// ---------------------------------------------------------------------------
// Low-level SPI helpers
// ---------------------------------------------------------------------------

void ILI9225::writeIndex(uint16_t reg)
{
    gpio_put(kPinRS, 0);  // RS low = index/command
    gpio_put(kPinCS, 0);
    uint8_t buf[2] = { static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF) };
    spi_write_blocking(LCD_SPI, buf, 2);
    gpio_put(kPinCS, 1);
}

void ILI9225::writeData16(uint16_t val)
{
    gpio_put(kPinRS, 1);  // RS high = data
    gpio_put(kPinCS, 0);
    uint8_t buf[2] = { static_cast<uint8_t>(val >> 8), static_cast<uint8_t>(val & 0xFF) };
    spi_write_blocking(LCD_SPI, buf, 2);
    gpio_put(kPinCS, 1);
}

void ILI9225::writeReg(uint16_t reg, uint16_t val)
{
    writeIndex(reg);
    writeData16(val);
}

// ---------------------------------------------------------------------------
// Hardware reset
// ---------------------------------------------------------------------------

void ILI9225::hardwareReset()
{
    gpio_put(kPinRST, 1);
    sleep_ms(10);
    gpio_put(kPinRST, 0);
    sleep_ms(50);
    gpio_put(kPinRST, 1);
    sleep_ms(120);
}

// ---------------------------------------------------------------------------
// GRAM window + pixel write
// ---------------------------------------------------------------------------

// setWindow - accepts logical landscape coordinates (220×176)
// and maps them to physical GRAM coordinates (176×220).
//
// Landscape rotation (90 degrees left):
//   Logical (lx, ly) -> GRAM horizontal = ly, GRAM vertical = 219 - lx
//   Entry mode AM=1, ID1=1, ID0=0 makes GRAM auto-advance correctly.
void ILI9225::setWindow(unsigned int x0, unsigned int y0,
                        unsigned int x1, unsigned int y1)
{
    // Map logical landscape coords -> physical GRAM coords
    // Landscape 90 degrees left with ID=11, AM=1:
    //   logical x -> physical vertical (row), logical y -> physical horizontal (column)
    unsigned int gramH0 = y0;               // physical column start
    unsigned int gramH1 = y1;               // physical column end
    unsigned int gramV0 = x0;               // physical row start
    unsigned int gramV1 = x1;               // physical row end

    writeReg(REG_HORIZ_WIN_ADDR1, gramH1);  // column end
    writeReg(REG_HORIZ_WIN_ADDR2, gramH0);  // column start
    writeReg(REG_VERT_WIN_ADDR1,  gramV1);  // row end
    writeReg(REG_VERT_WIN_ADDR2,  gramV0);  // row start

    // GRAM start address = top-left of logical window
    writeReg(REG_RAM_ADDR_SET1, gramH0);     // horizontal = y0
    writeReg(REG_RAM_ADDR_SET2, gramV0);     // vertical   = x0
}

void ILI9225::beginPixelWrite()
{
    writeIndex(REG_GRAM_DATA);
    gpio_put(kPinRS, 1);  // RS high for pixel data stream
    gpio_put(kPinCS, 0);  // CS stays low for the entire pixel burst
}

// ---------------------------------------------------------------------------
// DMA helpers
// ---------------------------------------------------------------------------

void ILI9225::waitDma()
{
    if (m_dmaChannel >= 0)
        dma_channel_wait_for_finish_blocking(m_dmaChannel);
}

void ILI9225::launchDma(const uint16_t* buf, unsigned int count)
{
    waitDma();

    dma_channel_config cfg = dma_channel_get_default_config(m_dmaChannel);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_dreq(&cfg, spi_get_dreq(LCD_SPI, true));
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);

    dma_channel_configure(
        m_dmaChannel,
        &cfg,
        &spi_get_hw(LCD_SPI)->dr,   // write to SPI TX FIFO
        buf,
        count * 2,                   // byte count (each pixel = 2 bytes)
        true                         // start immediately
    );
}

// ---------------------------------------------------------------------------
// Clear borders to black (called once after init)
// ---------------------------------------------------------------------------

void ILI9225::clearBorders()
{
    // Fill entire logical screen (220×176) with black
    setWindow(0, 0, kScreenW - 1, kScreenH - 1);
    beginPixelWrite();
    for (unsigned int i = 0; i < kScreenW * kScreenH; ++i)
    {
        uint8_t z[2] = {0, 0};
        spi_write_blocking(LCD_SPI, z, 2);
    }
    gpio_put(kPinCS, 1);
}

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

void ILI9225::init()
{
    // ---- GPIO setup --------------------------------------------------------
    // CS, RS, RST, LED as plain GPIO outputs
    gpio_init(kPinCS);   gpio_set_dir(kPinCS,  GPIO_OUT); gpio_put(kPinCS,  1);
    gpio_init(kPinRS);   gpio_set_dir(kPinRS,  GPIO_OUT); gpio_put(kPinRS,  1);
    gpio_init(kPinRST);  gpio_set_dir(kPinRST, GPIO_OUT); gpio_put(kPinRST, 1);
    gpio_init(kPinLED);  gpio_set_dir(kPinLED, GPIO_OUT); gpio_put(kPinLED, 0);

    // SPI0 at 10 MHz
    spi_init(LCD_SPI, 10 * 1000 * 1000);
    gpio_set_function(kPinSCK, GPIO_FUNC_SPI);
    gpio_set_function(kPinSDI, GPIO_FUNC_SPI);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // ---- Hardware reset ----------------------------------------------------
    hardwareReset();

    // ---- ILI9225 power-on sequence -----------------------------------------
    // Reference: ILI9225 datasheet application note / open-source Arduino lib
    writeReg(REG_POWER_CTRL1, 0x0000);  // SAP=0, power off
    writeReg(REG_POWER_CTRL2, 0x0000);
    writeReg(REG_POWER_CTRL3, 0x0000);
    writeReg(REG_POWER_CTRL4, 0x0000);
    writeReg(REG_POWER_CTRL5, 0x0000);
    sleep_ms(40);

    // Power on sequence
    writeReg(REG_POWER_CTRL2, 0x0018);  // VCI1 = 2.58V, reference voltage
    writeReg(REG_POWER_CTRL3, 0x6121);  // VRH step-up, VDV
    writeReg(REG_POWER_CTRL4, 0x006F);  // VCM
    writeReg(REG_POWER_CTRL5, 0x495F);  // VCOMH
    writeReg(REG_POWER_CTRL1, 0x0800);  // SAP=1, AP=000, BT=000
    sleep_ms(10);
    writeReg(REG_POWER_CTRL2, 0x103B);  // PON=1, VRN, VRP
    sleep_ms(50);

    // Driver output control
    writeReg(REG_DRIVER_OUTPUT, 0x031C);  // SS=1 (flip gate scan for landscape), NL=28 (220 lines)
    writeReg(REG_LCD_AC_DRIVING, 0x0100); // line inversion
    writeReg(REG_ENTRY_MODE, 0x1038);     // BGR=1, ID1=1, ID0=1, AM=1 (landscape 90 degrees left, flipped)
    writeReg(REG_DISPLAY_CTRL1, 0x0000);  // display off for now
    writeReg(REG_BLANK_PERIOD, 0x0808);   // front/back porch
    writeReg(REG_FRAME_CYCLE, 0x1100);    // clock cycle
    writeReg(REG_INTERFACE_CTRL, 0x0000); // system interface
    writeReg(REG_OSC_CTRL, 0x0D01);       // oscillator on
    writeReg(REG_VCI_RECYCLING, 0x0020);  // VCI recycling

    // GRAM scan
    writeReg(REG_GATE_SCAN_CTRL, 0x0000);
    writeReg(REG_VERT_SCROLL1, 0x00DB);  // 219
    writeReg(REG_VERT_SCROLL2, 0x0000);
    writeReg(REG_VERT_SCROLL3, 0x0000);
    writeReg(REG_PARTIAL_DRIVING1, 0x00DB);
    writeReg(REG_PARTIAL_DRIVING2, 0x0000);

    // Full window (physical GRAM dimensions: 176 columns × 220 rows)
    writeReg(REG_HORIZ_WIN_ADDR1, kPanelW - 1);   // 175
    writeReg(REG_HORIZ_WIN_ADDR2, 0x0000);
    writeReg(REG_VERT_WIN_ADDR1,  kPanelH - 1);   // 219
    writeReg(REG_VERT_WIN_ADDR2,  0x0000);

    // Gamma (neutral)
    writeReg(REG_GAMMA_CTRL1,  0x0000);
    writeReg(REG_GAMMA_CTRL2,  0x0808);
    writeReg(REG_GAMMA_CTRL3,  0x080A);
    writeReg(REG_GAMMA_CTRL4,  0x000A);
    writeReg(REG_GAMMA_CTRL5,  0x0A08);
    writeReg(REG_GAMMA_CTRL6,  0x0808);
    writeReg(REG_GAMMA_CTRL7,  0x0000);
    writeReg(REG_GAMMA_CTRL8,  0x0A00);
    writeReg(REG_GAMMA_CTRL9,  0x0710);
    writeReg(REG_GAMMA_CTRL10, 0x0710);

    // Display ON
    writeReg(REG_DISPLAY_CTRL1, 0x0012);
    sleep_ms(50);
    writeReg(REG_DISPLAY_CTRL1, 0x1017);  // GON=1, DTE=1, D=11

    // ---- Clear borders + entire screen to black ----------------------------
    clearBorders();

    // ---- Claim a DMA channel for line transfers ----------------------------
    m_dmaChannel = dma_claim_unused_channel(true);

    // ---- Backlight on ------------------------------------------------------
    gpio_put(kPinLED, 1);

    printf("ILI9225: init done, DMA ch=%d\n", m_dmaChannel);
}

// ---------------------------------------------------------------------------
// fillScreen() - test pattern helper
// ---------------------------------------------------------------------------

void ILI9225::fillScreen(uint16_t rgb565)
{
    setWindow(0, 0, kScreenW - 1, kScreenH - 1);
    beginPixelWrite();

    uint8_t hi = rgb565 >> 8;
    uint8_t lo = rgb565 & 0xFF;
    for (unsigned int i = 0; i < kScreenW * kScreenH; ++i)
    {
        uint8_t buf[2] = { hi, lo };
        spi_write_blocking(LCD_SPI, buf, 2);
    }
    gpio_put(kPinCS, 1);
}

// ---------------------------------------------------------------------------
// fillScreenBuffer() - send a full 220×176 RGB565 buffer to the display
// ---------------------------------------------------------------------------

void ILI9225::fillScreenBuffer(const uint16_t* rgb565)
{
    setWindow(0, 0, kScreenW - 1, kScreenH - 1);
    beginPixelWrite();

    for (unsigned int y = 0; y < kScreenH; ++y)
    {
        const uint16_t* srcRow = rgb565 + y * kScreenW;
        for (unsigned int x = 0; x < kScreenW; ++x)
        {
            uint16_t c = srcRow[x];
            uint8_t buf[2] = { static_cast<uint8_t>(c >> 8),
                               static_cast<uint8_t>(c & 0xFF) };
            spi_write_blocking(LCD_SPI, buf, 2);
        }
    }

    gpio_put(kPinCS, 1);
}

// ---------------------------------------------------------------------------
// drawFrame() - blit 160×144 framebuffer to LCD (1:1, centered)
// ---------------------------------------------------------------------------

#ifdef PICO_RP2040

// RP2040 variant: framebuffer is already RGB565 - just byte-swap for SPI and DMA.
void ILI9225::drawFrame(const uint16_t* rgb565)
{
    setWindow(kOffsetX, kOffsetY,
              kOffsetX + kGBW - 1,
              kOffsetY + kGBH - 1);

    beginPixelWrite();

    for (unsigned int y = 0; y < kGBH; ++y)
    {
        const uint16_t* srcRow = rgb565 + y * kGBW;
        uint16_t* dst = m_lineBuf[m_activeBuf];

        for (unsigned int x = 0; x < kGBW; ++x)
        {
            uint16_t c = srcRow[x];
            // Swap bytes for SPI (DMA sends bytes in memory order, SPI expects MSB first)
            dst[x] = (c >> 8) | (c << 8);
        }

        launchDma(dst, kGBW);
        m_activeBuf ^= 1;
    }

    waitDma();
    gpio_put(kPinCS, 1);
}

#else

// RP2350 / desktop variant: convert RGBA8888 -> RGB565 on the fly.
void ILI9225::drawFrame(const uint32_t* rgba)
{
    setWindow(kOffsetX, kOffsetY,
              kOffsetX + kGBW - 1,
              kOffsetY + kGBH - 1);

    beginPixelWrite();

    for (unsigned int y = 0; y < kGBH; ++y)
    {
        const uint32_t* srcRow = rgba + y * kGBW;
        uint16_t* dst = m_lineBuf[m_activeBuf];

        for (unsigned int x = 0; x < kGBW; ++x)
        {
            uint32_t px = srcRow[x];    // RGBA8888: 0xRRGGBBAA
            uint8_t r = (px >> 24) & 0xFF;
            uint8_t g = (px >> 16) & 0xFF;
            uint8_t b = (px >>  8) & 0xFF;
            uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            // Swap bytes for SPI (DMA sends bytes in memory order, SPI expects MSB first)
            dst[x] = (c >> 8) | (c << 8);
        }

        launchDma(dst, kGBW);
        m_activeBuf ^= 1;
    }

    waitDma();
    gpio_put(kPinCS, 1);
}

#endif
