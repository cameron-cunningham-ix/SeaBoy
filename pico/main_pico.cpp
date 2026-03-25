#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Stage 1 — minimal boot stub
// Verifies the SeaBoy core compiles for RP2350/RP2040 and the board boots.
// Prints a status line over USB serial; replace with the real emulation loop
// in later stages.
// ---------------------------------------------------------------------------

int main()
{
    // Overclock to 250 MHz — stable on both RP2040 and RP2350.
    // Required for 60 fps emulation; must be called before stdio_init_all()
    // because USB CDC re-init after clock change ensures correct baud timing.
    set_sys_clock_khz(250000, true);

    // Init USB serial (stdio_usb) — connect with any serial monitor at any baud.
    stdio_init_all();

    // Brief delay so the host has time to open the USB CDC port before we print.
    sleep_ms(2000);

#if defined(PICO_RP2040)
    printf("SeaBoy Pico1 RP2040 boot OK\n");
#else
    printf("SeaBoy Pico2 RP2350 boot OK\n");
#endif
    printf("sys_clk = %lu kHz\n", clock_get_hz(clk_sys) / 1000);

    // Placeholder loop — replaced by emulation loop in Stage 6.
    while (true)
    {
        tight_loop_contents();
    }
}
