#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "UIPlatform.h"

int main(int argc, char *argv[])
{
    char displayTitle[128] = "SeaBoy!";
    
    // Create emulation / debug window
    UIPlatform platform(displayTitle, 1280, 720, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    
   

    return 0;
}