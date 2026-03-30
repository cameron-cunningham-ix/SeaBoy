// Hardware configuration for carlk3's no-OS-FatFS-SD-SPI-RPi-Pico library.
// Defines the SPI1 pin mapping for the SD card slot.
//
// SPI1 wiring:
//   GPIO 12 -> MISO (RX)
//   GPIO 13 -> CS   (software chip-select)
//   GPIO 14 -> SCK
//   GPIO 15 -> MOSI (TX)

#include "hw_config.h"

// --- SPI configuration ---------------------------------------------------
static spi_t s_spis[] = {
    {
        .hw_inst    = spi1,
        .miso_gpio  = 12,
        .mosi_gpio  = 15,
        .sck_gpio   = 14,
        .baud_rate  = 12 * 1000 * 1000,  // 12 MHz - reliable for most SD cards
    }
};

// --- SD card configuration -----------------------------------------------
static sd_card_t s_sd_cards[] = {
    {
        .pcName          = "0:",
        .spi             = &s_spis[0],
        .ss_gpio         = 13,
        .use_card_detect = false,
        .card_detect_gpio = 0,
        .card_detected_true = 0,
    }
};

// --- Library callbacks ---------------------------------------------------

size_t sd_get_num(void)    { return 1; }
size_t spi_get_num(void)   { return 1; }

sd_card_t*  sd_get_by_num(size_t num)  { return (num == 0) ? &s_sd_cards[0] : NULL; }
spi_t*      spi_get_by_num(size_t num) { return (num == 0) ? &s_spis[0]     : NULL; }
