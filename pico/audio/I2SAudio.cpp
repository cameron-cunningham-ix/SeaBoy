#include "audio/I2SAudio.hpp"
#include "src/core/APU.hpp"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include <cstdio>
#include <cstring>

// Generated PIO header from i2s_out.pio (created by pico_generate_pio_header)
#include "i2s_out.pio.h"

// ---------------------------------------------------------------------------
// Module-level state (single audio instance)
// ---------------------------------------------------------------------------

static SeaBoy::APU* s_apu           = nullptr;
static PIO          s_pio           = pio0;
static uint         s_sm            = 0;
static uint         s_dmaCh0        = 0;
static uint         s_dmaCh1        = 0;

// Double-buffer: each buffer holds kBufFrames packed 32-bit I2S frames.
// Each frame = (int16_t left << 16) | (uint16_t right).
// For mono MAX98357A, left == right == mixed mono sample.
static constexpr unsigned int kBufFrames = 256;
static uint32_t s_buf0[kBufFrames];
static uint32_t s_buf1[kBufFrames];

// Flags set by DMA ISR, cleared by pump() in the main loop.
static volatile bool s_buf0Done = false;
static volatile bool s_buf1Done = false;

// ---------------------------------------------------------------------------
// Fill a buffer from APU samples
// ---------------------------------------------------------------------------
static void fillBuffer(uint32_t* buf)
{
    if (!s_apu)
    {
        std::memset(buf, 0, kBufFrames * sizeof(uint32_t));
        return;
    }

    uint32_t count;

#if defined(PICO_RP2040)
    // Integer path: drain Q15 int16 pairs and mix to mono without any soft-float.
    int16_t samples[kBufFrames * 2];
    count = s_apu->drainSamples(samples, kBufFrames);

    for (uint32_t i = 0; i < count; ++i)
    {
        // Mix stereo to mono: arithmetic right shift by 1 (rounds toward -inf, fine for audio)
        int32_t mono = (static_cast<int32_t>(samples[i * 2]) + static_cast<int32_t>(samples[i * 2 + 1])) >> 1;
        // Clamp to int16 range (overflow is theoretically impossible here but be safe)
        if (mono >  32767) mono =  32767;
        if (mono < -32767) mono = -32767;
        uint16_t u = static_cast<uint16_t>(static_cast<int16_t>(mono));
        buf[i] = (static_cast<uint32_t>(u) << 16) | u;
    }
#else
    // Float path: used on RP2350 (hardware FPU) and any non-RP2040 Pico builds.
    float samples[kBufFrames * 2];
    count = s_apu->drainSamples(samples, kBufFrames);

    for (uint32_t i = 0; i < count; ++i)
    {
        // Mix stereo to mono (MAX98357A is a mono amp)
        float mono = (samples[i * 2] + samples[i * 2 + 1]) * 0.5f;

        // Clamp to [-1, 1] range
        if (mono >  1.0f) mono =  1.0f;
        if (mono < -1.0f) mono = -1.0f;

        // Convert to signed 16-bit
        int16_t s = static_cast<int16_t>(mono * 32767.0f);
        uint16_t u = static_cast<uint16_t>(s);

        // Pack same value into both L and R channels
        buf[i] = (static_cast<uint32_t>(u) << 16) | u;
    }
#endif

    // Zero-fill any remainder (underrun)
    for (uint32_t i = count; i < kBufFrames; ++i)
        buf[i] = 0;
}

// ---------------------------------------------------------------------------
// DMA interrupt handler - fires when a buffer transfer completes
// ---------------------------------------------------------------------------
static void dmaIRQHandler()
{
    if (dma_irqn_get_channel_status(0, s_dmaCh0))
    {
        dma_irqn_acknowledge_channel(0, s_dmaCh0);
        s_buf0Done = true;
    }
    if (dma_irqn_get_channel_status(0, s_dmaCh1))
    {
        dma_irqn_acknowledge_channel(0, s_dmaCh1);
        s_buf1Done = true;
    }
}

// ---------------------------------------------------------------------------
// I2SAudio implementation
// ---------------------------------------------------------------------------

void I2SAudio::setAPU(SeaBoy::APU* apu)
{
    s_apu = apu;
}

void I2SAudio::init(uint32_t sampleRate)
{
    // ---- PIO setup --------------------------------------------------------

    // Load the I2S PIO program
    uint offset = pio_add_program(s_pio, &i2s_out_program);
    s_sm = pio_claim_unused_sm(s_pio, true);

    // Configure state machine
    pio_sm_config cfg = i2s_out_program_get_default_config(offset);

    // OUT pin: DIN (GPIO 26) - one pin for data output
    sm_config_set_out_pins(&cfg, kPinDIN, 1);
    pio_gpio_init(s_pio, kPinDIN);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, kPinDIN, 1, true);

    // Side-set pins: BCLK (GPIO 27) and LRC (GPIO 28) - two pins
    sm_config_set_sideset_pins(&cfg, kPinBCLK);
    pio_gpio_init(s_pio, kPinBCLK);
    pio_gpio_init(s_pio, kPinLRC);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, kPinBCLK, 2, true);

    // Autopull: shift out MSB first, 32-bit threshold, shift left
    sm_config_set_out_shift(&cfg, false, true, 32);

    // FIFO: join both FIFOs for TX (8 entries instead of 4)
    sm_config_set_fifo_join(&cfg, PIO_FIFO_JOIN_TX);

    // Clock divider: PIO clock = sampleRate * 32 bits * 2 cycles/bit
    float pioFreq = static_cast<float>(sampleRate) * 32.0f * 2.0f;
    float divider = static_cast<float>(clock_get_hz(clk_sys)) / pioFreq;
    sm_config_set_clkdiv(&cfg, divider);

    pio_sm_init(s_pio, s_sm, offset, &cfg);

    printf("I2S: PIO SM%u, rate=%lu Hz, div=%.2f\n", s_sm,
           static_cast<unsigned long>(sampleRate), static_cast<double>(divider));

    // ---- DMA setup --------------------------------------------------------

    s_dmaCh0 = dma_claim_unused_channel(true);
    s_dmaCh1 = dma_claim_unused_channel(true);

    // Channel 0: transfers buf0 to PIO TX FIFO, chains to channel 1
    dma_channel_config c0 = dma_channel_get_default_config(s_dmaCh0);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_chain_to(&c0, s_dmaCh1);
    dma_channel_configure(s_dmaCh0, &c0,
        &s_pio->txf[s_sm],  // write address: PIO TX FIFO
        s_buf0,              // read address: buffer 0
        kBufFrames,          // transfer count
        false);              // don't start yet

    // Channel 1: transfers buf1 to PIO TX FIFO, chains to channel 0
    dma_channel_config c1 = dma_channel_get_default_config(s_dmaCh1);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_dreq(&c1, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_chain_to(&c1, s_dmaCh0);
    dma_channel_configure(s_dmaCh1, &c1,
        &s_pio->txf[s_sm],  // write address: PIO TX FIFO
        s_buf1,              // read address: buffer 1
        kBufFrames,          // transfer count
        false);              // don't start yet

    // Enable IRQ0 for both channels so we know when to refill
    dma_channel_set_irq0_enabled(s_dmaCh0, true);
    dma_channel_set_irq0_enabled(s_dmaCh1, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dmaIRQHandler);
    irq_set_enabled(DMA_IRQ_0, true);

    printf("I2S: DMA ch%u + ch%u (chained), %u frames/buf\n",
           s_dmaCh0, s_dmaCh1, kBufFrames);
}

void I2SAudio::start()
{
    // Pre-fill both buffers with silence (or initial APU data)
    ::fillBuffer(s_buf0);
    ::fillBuffer(s_buf1);

    s_buf0Done = false;
    s_buf1Done = false;

    // Start DMA channel 0 (will chain to 1, then back to 0, etc.)
    dma_channel_set_read_addr(s_dmaCh0, s_buf0, false);
    dma_channel_set_trans_count(s_dmaCh0, kBufFrames, false);
    dma_channel_set_read_addr(s_dmaCh1, s_buf1, false);
    dma_channel_set_trans_count(s_dmaCh1, kBufFrames, false);

    // Enable PIO state machine
    pio_sm_set_enabled(s_pio, s_sm, true);

    // Kick off DMA channel 0
    dma_channel_start(s_dmaCh0);

    printf("I2S: playback started\n");
}

bool I2SAudio::pump()
{
    bool refilled = false;

    if (s_buf0Done)
    {
        ::fillBuffer(s_buf0);
        // Reset read address for next DMA transfer (chained DMA reloads
        // the config from the channel registers, so we must update now).
        dma_channel_set_read_addr(s_dmaCh0, s_buf0, false);
        dma_channel_set_trans_count(s_dmaCh0, kBufFrames, false);
        s_buf0Done = false;
        refilled = true;
    }

    if (s_buf1Done)
    {
        ::fillBuffer(s_buf1);
        dma_channel_set_read_addr(s_dmaCh1, s_buf1, false);
        dma_channel_set_trans_count(s_dmaCh1, kBufFrames, false);
        s_buf1Done = false;
        refilled = true;
    }

    return refilled;
}
