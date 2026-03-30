#pragma once

#include <cstdint>

namespace SeaBoy { class APU; }

// I2S audio output driver for MAX98357A mono amplifier.
//
// Uses PIO for I2S timing and DMA double-buffering for gapless playback.
// Drains float stereo samples from the APU, mixes to mono, converts to
// 16-bit signed, and packs into 32-bit I2S frames (same value L+R).
//
// GPIO assignment:
//   GPIO 26 = DIN  (I2S data)
//   GPIO 27 = BCLK (I2S bit clock)
//   GPIO 28 = LRC  (I2S word select / LRCLK)

class I2SAudio
{
public:
    // Initialize PIO, DMA, and pin configuration.
    // sampleRate: 48000 for RP2350, 22050 for RP2040.
    void init(uint32_t sampleRate);

    // Begin DMA playback. Call after init() and setAPU().
    void start();

    // Set the APU instance to drain samples from.
    void setAPU(SeaBoy::APU* apu);

    // Call from the main loop to refill any completed DMA buffers.
    // Returns true if a buffer was refilled.
    bool pump();

private:
    // Pin assignments
    static constexpr unsigned int kPinDIN  = 26;
    static constexpr unsigned int kPinBCLK = 27;
    static constexpr unsigned int kPinLRC  = 28;

    // DMA buffer size: 256 stereo frames per buffer (1 KB each)
    static constexpr unsigned int kBufFrames = 256;
};
