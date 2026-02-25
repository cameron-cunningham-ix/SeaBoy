#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "src/ui/UIPlatform.h"

int main(int argc, char *argv[])
{
    const int DISPLAY_WIDTH = 160;
    const int DISPLAY_HEIGHT = 144;
    char displayTitle[128] = "SeaBoy!";
    
    
    // Create emulation / debug window
    UIPlatform platform(displayTitle, 1280, 720, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    bool running = true;
    while (running)
    {
        if (!platform.processInput())
        {
            running = false;
        }
        platform.renderUI();

    }
    
   

    return 0;
}