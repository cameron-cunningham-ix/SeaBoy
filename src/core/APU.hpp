#pragma once

#include <cstdint>

// PanDocs Audio - Game Boy Audio Processing Unit
// https://gbdev.io/pandocs/Audio.html
//
// The APU generates sound through 4 channels:
//   CH1: Pulse with frequency sweep  (NR10-NR14)
//   CH2: Pulse                        (NR21-NR24)
//   CH3: Wave (plays samples from Wave RAM) (NR30-NR34)
//   CH4: Noise (LFSR-based)          (NR41-NR44)
//
// Global registers:
//   NR50 (0xFF24): Master volume + VIN routing
//   NR51 (0xFF25): Sound panning (L/R per channel)
//   NR52 (0xFF26): APU master control (bit 7 power, bits 0-3 channel status)
//
// Wave RAM: 0xFF30-0xFF3F (16 bytes = 32 4-bit samples)
//
// The frame sequencer is clocked by a falling edge on bit 12 of the Timer's
// 16-bit system counter (DIV-APU), producing an 8-step sequence at 512 Hz:
//   Steps 0,2,4,6: clock length timers (256 Hz)
//   Steps 2,6:     clock CH1 sweep     (128 Hz)
//   Step 7:        clock envelopes     (64 Hz)

namespace SeaBoy
{
    class MMU; // forward declaration

    class APU
    {
    public:
        explicit APU(MMU& mmu);

        // Reset to power-on state.
        void reset();

        // Advance APU by tCycles T-cycles. sysCounter is Timer's current 16-bit counter
        // (needed for DIV-APU falling-edge detection on bit 12).
        void tick(uint32_t tCycles, uint16_t sysCounter);

        // I/O register interface - called by MMU for 0xFF10-0xFF26 and 0xFF30-0xFF3F.
        uint8_t read(uint16_t addr) const;
        void    write(uint16_t addr, uint8_t val);

        // Drain audio samples into outBuffer (interleaved stereo float pairs).
        // Returns number of stereo pairs written (up to maxPairs).
        uint32_t drainSamples(float* outBuffer, uint32_t maxPairs);

    private:
        // -- Duty cycle table --------------------------------------------
        // PanDocs Audio Registers - NR11
        static constexpr uint8_t kDutyTable[4][8] = {
            {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
            {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
            {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
            {0, 1, 1, 1, 1, 1, 1, 0}, // 75%
        };

        // -- Channel structs ---------------------------------------------

        // PanDocs Audio Registers - Pulse channel (CH1/CH2)
        struct PulseChannel
        {
            // Registers
            uint8_t nrx1 = 0x00; // duty + length load
            uint8_t nrx2 = 0x00; // volume envelope
            uint8_t nrx3 = 0x00; // period low (write-only)
            uint8_t nrx4 = 0x00; // period high + control

            // Running state
            bool     active       = false;
            uint8_t  dutyStep     = 0;     // 0-7; NOT reset on trigger
            uint8_t  lengthTimer  = 0;     // up-counter 0..64
            bool     lengthEnable = false;
            uint8_t  volume       = 0;     // 0-15 live volume
            uint8_t  envTimer     = 0;     // envelope countdown
            uint16_t period       = 0;     // 11-bit from NRx3/NRx4
            uint16_t periodTimer  = 0;     // up-counter for period divider

            bool dacEnabled() const { return (nrx2 & 0xF8u) != 0; }
            uint8_t dutyMode() const { return (nrx1 >> 6) & 0x03u; }
            uint8_t dutyOutput() const { return kDutyTable[dutyMode()][dutyStep]; }
            uint8_t output() const { return active ? (dutyOutput() ? volume : 0) : 0; }

            void tickPeriod()
            {
                ++periodTimer;
                if (periodTimer > 0x7FFu)
                {
                    periodTimer = period;
                    dutyStep = (dutyStep + 1) & 7;
                }
            }

            void clockLength()
            {
                if (!lengthEnable) return;
                if (lengthTimer < 64)
                {
                    ++lengthTimer;
                    if (lengthTimer >= 64) active = false;
                }
            }

            void clockEnvelope()
            {
                uint8_t pace = nrx2 & 0x07u;
                if (pace == 0) return;
                if (envTimer > 0) --envTimer;
                if (envTimer == 0)
                {
                    envTimer = pace;
                    uint8_t dir = (nrx2 >> 3) & 1u;
                    if (dir == 1 && volume < 15) ++volume;
                    if (dir == 0 && volume > 0)  --volume;
                }
            }
        };

        // PanDocs Audio Details - Pulse channel with sweep (CH1 only)
        struct SweepUnit
        {
            uint16_t shadow  = 0;
            uint8_t  timer   = 0;
            bool     enabled = false;
            bool     negUsed = false; // set when subtraction mode used since last trigger

            uint16_t calcNewPeriod(uint8_t step, uint8_t dir) const
            {
                uint16_t shifted = shadow >> step;
                return (dir == 1) ? static_cast<uint16_t>(shadow - shifted)
                                  : static_cast<uint16_t>(shadow + shifted);
            }
        };

        // PanDocs Audio Registers - Wave channel (CH3)
        struct WaveChannel
        {
            uint8_t  nr30 = 0x00; // DAC enable (bit 7)
            uint8_t  nr32 = 0x00; // output level (bits 6-5)
            uint8_t  nrx3 = 0x00; // period low (write-only)
            uint8_t  nrx4 = 0x00; // period high + control

            bool     active       = false;
            uint16_t lengthTimer  = 0;     // 0..256 (wider than pulse)
            bool     lengthEnable = false;
            uint16_t period       = 0;     // 11-bit
            uint16_t periodTimer  = 0;     // up-counter clocked at 2 MHz
            uint8_t  sampleIndex  = 0;     // 0-31
            uint8_t  sampleBuffer = 0;     // last nibble read

            bool dacEnabled() const { return (nr30 >> 7) & 1; }

            uint8_t outputShift() const
            {
                // PanDocs Audio Registers - NR32
                static constexpr uint8_t kShifts[4] = {4, 0, 1, 2};
                return kShifts[(nr32 >> 5) & 0x03u];
            }

            uint8_t output() const
            {
                if (!active) return 0;
                return sampleBuffer >> outputShift();
            }

            void clockLength()
            {
                if (!lengthEnable) return;
                if (lengthTimer < 256)
                {
                    ++lengthTimer;
                    if (lengthTimer >= 256) active = false;
                }
            }
        };

        // PanDocs Audio Details - Noise channel (CH4)
        struct NoiseChannel
        {
            uint8_t  nrx1 = 0x00; // length timer (write-only)
            uint8_t  nrx2 = 0x00; // volume envelope
            uint8_t  nr43 = 0x00; // frequency + LFSR width
            uint8_t  nrx4 = 0x00; // control

            bool     active       = false;
            uint8_t  lengthTimer  = 0;     // 0..64
            bool     lengthEnable = false;
            uint8_t  volume       = 0;     // 0-15
            uint8_t  envTimer     = 0;
            uint16_t lfsr         = 0;     // 15-bit LFSR state
            uint32_t freqTimer    = 0;     // down-counter for LFSR clocking

            bool dacEnabled() const { return (nrx2 & 0xF8u) != 0; }

            uint8_t output() const
            {
                if (!active) return 0;
                return ((lfsr & 1) == 0) ? volume : 0;
            }

            uint32_t calcPeriod() const
            {
                // PanDocs Audio Registers - NR43
                uint8_t r = nr43 & 0x07u;
                uint8_t s = (nr43 >> 4) & 0x0Fu;
                return static_cast<uint32_t>(r == 0 ? 8 : 16 * r) << s;
            }

            void tickLFSR()
            {
                // PanDocs Audio Details - Noise channel (CH4)
                uint8_t bit0     = (lfsr >> 0) & 1u;
                uint8_t bit1     = (lfsr >> 1) & 1u;
                uint8_t feedback = (bit0 == bit1) ? 1u : 0u; // XNOR

                lfsr |= static_cast<uint16_t>(feedback << 15);
                if ((nr43 >> 3) & 1u) // 7-bit mode
                    lfsr = static_cast<uint16_t>((lfsr & ~(1u << 7)) | (feedback << 7));

                lfsr >>= 1;
            }

            void clockLength()
            {
                if (!lengthEnable) return;
                if (lengthTimer < 64)
                {
                    ++lengthTimer;
                    if (lengthTimer >= 64) active = false;
                }
            }

            void clockEnvelope()
            {
                uint8_t pace = nrx2 & 0x07u;
                if (pace == 0) return;
                if (envTimer > 0) --envTimer;
                if (envTimer == 0)
                {
                    envTimer = pace;
                    uint8_t dir = (nrx2 >> 3) & 1u;
                    if (dir == 1 && volume < 15) ++volume;
                    if (dir == 0 && volume > 0)  --volume;
                }
            }
        };

        // -- Private methods ---------------------------------------------

        void advanceFrameSequencer();
        void clockLength();
        void clockSweep();
        void clockEnvelope();

        void triggerCh1();
        void triggerCh2();
        void triggerCh3();
        void triggerCh4();

        void powerOff();
        void powerOn();

        void generateSample();

        // -- State -------------------------------------------------------

        MMU& m_mmu;

        // Global APU state
        bool    m_powered       = false; // NR52 bit 7
        uint8_t m_nr50          = 0x00;  // 0xFF24 Master volume
        uint8_t m_nr51          = 0x00;  // 0xFF25 Sound panning
        uint8_t m_nr10          = 0x00;  // 0xFF10 CH1 sweep

        // Frame sequencer - PanDocs Audio Details - DIV-APU
        uint8_t m_frameSeqStep  = 0;     // 0-7
        bool    m_prevDivBit    = false;  // bit 12 of previous sysCounter

        // Channels
        PulseChannel m_ch1;
        PulseChannel m_ch2;
        WaveChannel  m_ch3;
        NoiseChannel m_ch4;
        SweepUnit    m_sweep; // CH1 only

        // Wave RAM - always accessible, not cleared by power-off
        uint8_t m_waveRam[16]{};

        // Audio sample output
        static constexpr uint32_t SAMPLE_BUFFER_SIZE = 4096;
        float    m_sampleBuffer[SAMPLE_BUFFER_SIZE * 2]{}; // interleaved L, R
        uint32_t m_sampleWritePos = 0;
        uint32_t m_sampleReadPos  = 0;
        uint32_t m_sampleTimer    = 0; // fractional accumulator for downsampling

        // High-pass filter state - PanDocs Audio Details - Mixer
        double m_hpfCapLeft  = 0.0;
        double m_hpfCapRight = 0.0;
    };

} // namespace SeaBoy
