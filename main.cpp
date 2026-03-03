#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "src/ui/UIPlatform.h"
#include "src/core/GameBoy.hpp"

int main(int argc, char *argv[])
{
    const int DISPLAY_WIDTH  = 160;
    const int DISPLAY_HEIGHT = 144;
    char displayTitle[128]   = "SeaBoy!";

    // Create emulation / debug window
    UIPlatform platform(displayTitle, 1280, 720, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    SeaBoy::GameBoy gameBoy;

    // Optionally load a ROM passed as a command-line argument
    if (argc > 1)
    {
        if (!gameBoy.loadROM(argv[1]))
            fprintf(stderr, "Warning: could not load ROM '%s'\n", argv[1]);
    }

    bool running = true;
    while (running)
    {
        if (!platform.processInput())
        {
            running = false;
            break;
        }

        // Run one full frame worth of T-cycles
        // PanDocs.4.8 — 154 lines × 456 T-cycles = 70 224 T-cycles per frame
        uint32_t frameCycles = 0;
        while (frameCycles < SeaBoy::TCYCLES_PER_FRAME)
        {
            frameCycles += gameBoy.tick();
        }

        // Push the PPU framebuffer to the display texture
        platform.writeToBuffer(gameBoy.getFrameBuffer());
        platform.renderUI();
    }

    return 0;
}