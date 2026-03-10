#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "src/ui/UIPlatform.hpp"
#include "src/ui/DebuggerUI.hpp"
#include "src/core/GameBoy.hpp"

int main(int argc, char *argv[])
{
    const int DISPLAY_WIDTH  = 160;
    const int DISPLAY_HEIGHT = 144;
    char displayTitle[128]   = "SeaBoy!";

    // Create emulation / debug window (wider to accommodate debugger panels)
    UIPlatform platform(displayTitle, 1280, 720, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    SeaBoy::GameBoy gameBoy;

    // Optionally load a ROM passed as a command-line argument
    if (argc > 1)
    {
        if (gameBoy.loadROM(argv[1]))
            platform.m_currentROMPath = argv[1];
        else
            fprintf(stderr, "Warning: could not load ROM '%s'\n", argv[1]);
    }

    // SDL3 audio stream for APU output — PanDocs Audio
    SDL_AudioSpec audioSpec{};
    audioSpec.freq     = 48000;
    audioSpec.format   = SDL_AUDIO_F32;
    audioSpec.channels = 2;
    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, nullptr, nullptr);
    if (audioStream)
        SDL_ResumeAudioStreamDevice(audioStream);
    else
        fprintf(stderr, "Warning: SDL audio failed to initialize: %s\n", SDL_GetError());

    DebuggerUI debugger(gameBoy, platform.getRenderer());
    platform.setDebugger(&debugger);
    platform.setGameBoy(&gameBoy);

    bool running = true;
    while (running)
    {
        if (!platform.processInput(&gameBoy))
        {
            running = false;
            break;
        }

        // Handle ROM open from File menu
        if (!platform.m_pendingROMPath.empty())
        {
            if (gameBoy.loadROM(platform.m_pendingROMPath))
                platform.m_currentROMPath = platform.m_pendingROMPath;
            else
                fprintf(stderr, "Warning: could not load ROM '%s'\n", platform.m_pendingROMPath.c_str());
            platform.m_pendingROMPath.clear();
        }

        // Handle Restart ROM
        if (platform.m_pendingRestart)
        {
            if (!platform.m_currentROMPath.empty())
                gameBoy.loadROM(platform.m_currentROMPath);
            platform.m_pendingRestart = false;
        }

        // Audio-driven sync — skip when paused to avoid blocking the UI
        if (audioStream && !debugger.isPaused())
        {
            constexpr int MAX_QUEUED_BYTES = 1608 * 2 * sizeof(float);
            while (SDL_GetAudioStreamQueued(audioStream) > MAX_QUEUED_BYTES)
            {
                SDL_Delay(1);
            }
        }

        // Emulation tick — respects pause / step / breakpoints
        if (!debugger.isPaused())
        {
            // Run one full frame worth of T-cycles
            // PanDocs.4.8 — 154 lines × 456 T-cycles = 70 224 T-cycles per frame
            uint32_t frameCycles = 0;
            while (frameCycles < SeaBoy::TCYCLES_PER_FRAME)
            {
                frameCycles += gameBoy.tick();
                if (!debugger.breakpointsEmpty() &&
                    debugger.checkBreakpoints(gameBoy.cpu().registers().PC))
                {
                    debugger.pause();
                    break;
                }
            }
        }
        else if (debugger.consumeStep())
        {
            gameBoy.tick();
        }
        else if (debugger.consumeStepFrame())
        {
            uint32_t frameCycles = 0;
            while (frameCycles < SeaBoy::TCYCLES_PER_FRAME)
                frameCycles += gameBoy.tick();
        }

        // Drain APU samples into SDL audio stream
        if (audioStream)
        {
            float samples[2048]; // up to 1024 stereo pairs
            uint32_t count = gameBoy.apu().drainSamples(samples, 1024);
            if (count > 0)
                SDL_PutAudioStreamData(audioStream, samples,
                    static_cast<int>(count * 2 * sizeof(float)));
        }

        // Push the PPU framebuffer to the display texture
        platform.writeToBuffer(gameBoy.getFrameBuffer());
        platform.renderUI([&]() { debugger.render(); });
    }

    return 0;
}