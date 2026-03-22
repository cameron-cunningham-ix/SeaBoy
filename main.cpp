#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "SDL3/SDL.h"
#include "SDL3/SDL_main.h"
#include "src/ui/UIPlatform.hpp"
#include "src/ui/DebuggerUI.hpp"
#include "src/core/GameBoy.hpp"
#include "src/core/SaveState.hpp"
#include "src/cartridge/Cartridge.hpp"

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
        gameBoy.setHardwareMode(platform.m_hardwareMode);
        if (gameBoy.loadROM(argv[1]))
            platform.m_currentROMPath = argv[1];
        else
            fprintf(stderr, "Warning: could not load ROM '%s'\n", argv[1]);
    }

    // SDL3 audio stream for APU output - PanDocs Audio
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

    int prevSpeedPercent = platform.m_speedPercent;
    uint64_t frameStartNs = SDL_GetTicksNS();

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
            // Auto-save SRAM for the current ROM before switching
            if (!platform.m_currentSavePath.empty() &&
                gameBoy.mmu().cartridge() && gameBoy.mmu().cartridge()->sramSize() > 0)
            {
                gameBoy.saveSRAM(platform.m_currentSavePath);
            }

            gameBoy.setHardwareMode(platform.m_hardwareMode);
            if (gameBoy.loadROM(platform.m_pendingROMPath))
            {
                platform.m_currentROMPath = platform.m_pendingROMPath;
                platform.m_currentSavePath = SeaBoy::SaveFile::getSavePath(platform.m_currentROMPath);
                if (platform.m_startPaused)
                    debugger.pause();
            }
            else
                fprintf(stderr, "Warning: could not load ROM '%s'\n", platform.m_pendingROMPath.c_str());
            platform.m_pendingROMPath.clear();
        }

        // Handle Restart ROM
        if (platform.m_pendingRestart)
        {
            if (!platform.m_currentROMPath.empty())
            {
                gameBoy.setHardwareMode(platform.m_hardwareMode);
                gameBoy.loadROM(platform.m_currentROMPath);
                if (platform.m_startPaused)
                    debugger.pause();
                // If user chose a custom .sav via "Load Save File...", load it
                // (overrides the default .sav that loadROM auto-loaded).
                std::string defaultSav = SeaBoy::SaveFile::getSavePath(platform.m_currentROMPath);
                if (platform.m_currentSavePath != defaultSav)
                    gameBoy.loadSRAM(platform.m_currentSavePath);
                else
                    platform.m_currentSavePath = defaultSav;
            }
            platform.m_pendingRestart = false;
        }

        // Flush stale audio when speed changes to prevent distortion on transitions
        if (audioStream && platform.m_speedPercent != prevSpeedPercent)
        {
            SDL_ClearAudioStream(audioStream);
            prevSpeedPercent = platform.m_speedPercent;
        }

        // Emulation tick - respects pause / step / breakpoints
        bool romLoaded = gameBoy.mmu().cartridge() != nullptr;
        if (romLoaded && !debugger.isPaused())
        {
            // Run one full frame worth of T-cycles
            // PanDocs.4.8 - 154 lines × 456 T-cycles = 70 224 T-cycles per frame
            uint32_t frameCycles = 0;
            while (frameCycles < SeaBoy::TCYCLES_PER_FRAME)
            {
                frameCycles += gameBoy.tick();

                SeaBoy::WatchHit watchHit{};
                if (gameBoy.hasWatchpoints() && gameBoy.consumePendingWatch(watchHit))
                {
                    debugger.notifyWatchHit(watchHit);
                    debugger.pause();
                    break;
                }

                if (!debugger.breakpointsEmpty() &&
                    debugger.checkBreakpoints(gameBoy.cpu().registers().PC))
                {
                    debugger.pause();
                    break;
                }
            }
        }
        else if (romLoaded && debugger.consumeStep())
        {
            static_cast<void>(gameBoy.tick());
            SeaBoy::WatchHit watchHit{};
            if (gameBoy.hasWatchpoints() && gameBoy.consumePendingWatch(watchHit))
            {
                debugger.notifyWatchHit(watchHit);
                debugger.pause();
            }
        }
        else if (romLoaded && debugger.consumeStepFrame())
        {
            uint32_t frameCycles = 0;
            while (frameCycles < SeaBoy::TCYCLES_PER_FRAME)
                frameCycles += gameBoy.tick();
        }
        else if (romLoaded)
        {
            int n = debugger.consumeStepNInstr();
            if (n > 0)
            {
                for (int i = 0; i < n; ++i)
                {
                    gameBoy.tick();
                    SeaBoy::WatchHit watchHit{};
                    if (gameBoy.hasWatchpoints() && gameBoy.consumePendingWatch(watchHit))
                    {
                        debugger.notifyWatchHit(watchHit);
                        debugger.pause();
                        break;
                    }
                    if (!debugger.breakpointsEmpty() &&
                        debugger.checkBreakpoints(gameBoy.cpu().registers().PC))
                    {
                        debugger.pause();
                        break;
                    }
                }
            }
        }

        // Drain APU samples into SDL audio stream.
        // Cap the queue to ~4 frames of audio to prevent unbounded growth at >100%.
        // If the queue overflows the cap, flush it so audio stays roughly in sync.
        if (audioStream)
        {
            float samples[2048]; // up to 1024 stereo pairs
            uint32_t count = gameBoy.apu().drainSamples(samples, 1024);
            if (count > 0)
            {
                constexpr int MAX_QUEUED_BYTES = 800 * 4 * 2 * static_cast<int>(sizeof(float));
                if (SDL_GetAudioStreamQueued(audioStream) > MAX_QUEUED_BYTES)
                    SDL_ClearAudioStream(audioStream);
                SDL_PutAudioStreamData(audioStream, samples,
                    static_cast<int>(count * 2 * sizeof(float)));
            }
        }

        // Wall-clock throttle: pace each frame to the target duration based on speed.
        // 70224 T-cycles @ 4.194304 MHz ≈ 16742 µs per frame at 1×.
        if (!debugger.isPaused())
        {
            constexpr uint64_t BASE_FRAME_NS = 16742000ULL;
            uint64_t targetNs = BASE_FRAME_NS * 100ULL / static_cast<uint64_t>(platform.m_speedPercent);
            uint64_t now = SDL_GetTicksNS();
            uint64_t elapsed = now - frameStartNs;
            if (elapsed < targetNs)
                SDL_DelayNS(targetNs - elapsed);
            frameStartNs = SDL_GetTicksNS();
        }
        else
        {
            frameStartNs = SDL_GetTicksNS();
        }

        // Push the PPU framebuffer to the display texture
        platform.writeToBuffer(gameBoy.getFrameBuffer());
        platform.renderUI([&]() { debugger.render(); });
    }

    // Auto-save SRAM on exit
    if (!platform.m_currentSavePath.empty() &&
        gameBoy.mmu().cartridge() && gameBoy.mmu().cartridge()->sramSize() > 0)
    {
        gameBoy.saveSRAM(platform.m_currentSavePath);
    }

    return 0;
}