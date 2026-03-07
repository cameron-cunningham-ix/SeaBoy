#include "APU.hpp"
#include "MMU.hpp"

namespace SeaBoy
{
    // -- Read masks for APU registers ------------------------------------
    // OR'd on read; 1 bits = unused/write-only, always read as 1.
    // PanDocs Audio Registers
    static constexpr uint8_t kReadMask[23] = {
        0x80, // 0xFF10 NR10
        0x3F, // 0xFF11 NR11
        0x00, // 0xFF12 NR12
        0xFF, // 0xFF13 NR13 (write-only)
        0xBF, // 0xFF14 NR14
        0xFF, // 0xFF15 unused
        0x3F, // 0xFF16 NR21
        0x00, // 0xFF17 NR22
        0xFF, // 0xFF18 NR23 (write-only)
        0xBF, // 0xFF19 NR24
        0x7F, // 0xFF1A NR30
        0xFF, // 0xFF1B NR31 (write-only)
        0x9F, // 0xFF1C NR32
        0xFF, // 0xFF1D NR33 (write-only)
        0xBF, // 0xFF1E NR34
        0xFF, // 0xFF1F unused
        0xFF, // 0xFF20 NR41 (write-only)
        0x00, // 0xFF21 NR42
        0x00, // 0xFF22 NR43
        0xBF, // 0xFF23 NR44
        0x00, // 0xFF24 NR50
        0x00, // 0xFF25 NR51
        0x70, // 0xFF26 NR52 (bits 6–4 always 1)
    };

    APU::APU(MMU& mmu)
        : m_mmu(mmu)
    {
        reset();
    }

    void APU::reset()
    {
        m_powered      = false;
        m_nr50         = 0x00;
        m_nr51         = 0x00;
        m_nr10         = 0x00;
        m_frameSeqStep = 0;
        m_prevDivBit   = false;

        m_ch1 = PulseChannel{};
        m_ch2 = PulseChannel{};
        m_ch3 = WaveChannel{};
        m_ch4 = NoiseChannel{};
        m_sweep = SweepUnit{};

        // Wave RAM is NOT cleared by reset (hardware-dependent garbage on power-on).
        // But for emulator determinism, zero it on hard reset.
        for (auto& b : m_waveRam) b = 0x00;

        m_sampleWritePos = 0;
        m_sampleReadPos  = 0;
        m_sampleTimer    = 0;
        m_hpfCapLeft     = 0.0;
        m_hpfCapRight    = 0.0;
    }

    // -- Tick ------------------------------------------------------------

    void APU::tick(uint32_t tCycles, uint16_t sysCounter)
    {
        // Frame sequencer: detect falling edge of bit 12 of sysCounter (DIV-APU).
        // This runs even when APU is powered off (tracks DIV-APU edges).
        // PanDocs Audio Details - DIV-APU
        bool currBit = ((sysCounter >> 12) & 1) != 0;
        if (m_prevDivBit && !currBit)
        {
            if (m_powered)
                advanceFrameSequencer();
        }
        m_prevDivBit = currBit;

        if (!m_powered) return;

        // Tick channel period dividers.
        // Pulse channels: clocked at 1,048,576 Hz (once per 4 T-cycles).
        // Wave channel:   clocked at 2,097,152 Hz (once per 2 T-cycles).
        // Noise channel:  clocked per T-cycle (internal timer).
        for (uint32_t t = 0; t < tCycles; ++t)
        {
            // Noise channel - per T-cycle
            if (m_ch4.active)
            {
                uint8_t s = (m_ch4.nr43 >> 4) & 0x0Fu;
                if (s < 14) // s=14,15 stops clocking
                {
                    if (m_ch4.freqTimer > 0) --m_ch4.freqTimer;
                    if (m_ch4.freqTimer == 0)
                    {
                        m_ch4.freqTimer = m_ch4.calcPeriod();
                        m_ch4.tickLFSR();
                    }
                }
            }

            // Wave channel - every 2 T-cycles
            if ((t & 1) == 1 && m_ch3.active)
            {
                ++m_ch3.periodTimer;
                if (m_ch3.periodTimer > 0x7FFu)
                {
                    m_ch3.periodTimer = m_ch3.period;
                    m_ch3.sampleIndex = (m_ch3.sampleIndex + 1) & 31;
                    uint8_t byteIdx = m_ch3.sampleIndex >> 1;
                    uint8_t byte    = m_waveRam[byteIdx];
                    m_ch3.sampleBuffer = (m_ch3.sampleIndex & 1)
                        ? static_cast<uint8_t>(byte & 0x0Fu)
                        : static_cast<uint8_t>(byte >> 4);
                }
            }

            // Pulse channels - every 4 T-cycles
            if ((t & 3) == 3)
            {
                if (m_ch1.active) m_ch1.tickPeriod();
                if (m_ch2.active) m_ch2.tickPeriod();
            }

            // Downsample to 48 kHz - PanDocs Audio Details - Mixer
            m_sampleTimer += 48000;
            if (m_sampleTimer >= 4194304)
            {
                m_sampleTimer -= 4194304;
                generateSample();
            }
        }
    }

    // -- Frame Sequencer -------------------------------------------------

    void APU::advanceFrameSequencer()
    {
        // PanDocs Audio Details - DIV-APU
        switch (m_frameSeqStep)
        {
            case 0: clockLength(); break;
            case 1: break;
            case 2: clockLength(); clockSweep(); break;
            case 3: break;
            case 4: clockLength(); break;
            case 5: break;
            case 6: clockLength(); clockSweep(); break;
            case 7: clockEnvelope(); break;
        }
        m_frameSeqStep = (m_frameSeqStep + 1) & 7;
    }

    void APU::clockLength()
    {
        // PanDocs Audio Details - DIV-APU, length timer
        m_ch1.clockLength();
        m_ch2.clockLength();
        m_ch3.clockLength();
        m_ch4.clockLength();
    }

    void APU::clockSweep()
    {
        // PanDocs Audio Details - Pulse channel with sweep (CH1)
        if (!m_sweep.enabled) return;

        if (m_sweep.timer > 0) --m_sweep.timer;
        if (m_sweep.timer != 0) return;

        uint8_t pace = (m_nr10 >> 4) & 0x07u;
        m_sweep.timer = (pace != 0) ? pace : 8;

        if (pace == 0) return;

        uint8_t step = m_nr10 & 0x07u;
        uint8_t dir  = (m_nr10 >> 3) & 1u;

        uint16_t newPeriod = m_sweep.calcNewPeriod(step, dir);
        if (dir == 1) m_sweep.negUsed = true;

        // Overflow check - PanDocs Audio Details - APU-05
        if (newPeriod > 0x7FFu)
        {
            m_ch1.active = false;
            return;
        }

        if (step != 0)
        {
            m_sweep.shadow = newPeriod;
            m_ch1.period   = newPeriod;
            m_ch1.nrx3     = static_cast<uint8_t>(newPeriod & 0xFFu);
            m_ch1.nrx4     = static_cast<uint8_t>((m_ch1.nrx4 & 0xF8u) | ((newPeriod >> 8) & 0x07u));

            // Second overflow check - result discarded but can disable channel
            uint16_t check = m_sweep.calcNewPeriod(step, dir);
            if (dir == 1) m_sweep.negUsed = true;
            if (check > 0x7FFu)
                m_ch1.active = false;
        }
    }

    void APU::clockEnvelope()
    {
        // PanDocs Audio Details - DIV-APU, envelope
        m_ch1.clockEnvelope();
        m_ch2.clockEnvelope();
        // CH3 has no envelope
        m_ch4.clockEnvelope();
    }

    // -- Triggers --------------------------------------------------------

    void APU::triggerCh1()
    {
        // PanDocs Audio Registers - NR14 trigger
        if (m_ch1.dacEnabled())
            m_ch1.active = true;

        if (m_ch1.lengthTimer >= 64)
            m_ch1.lengthTimer = 0;

        m_ch1.periodTimer = m_ch1.period;
        m_ch1.volume      = (m_ch1.nrx2 >> 4) & 0x0Fu;
        m_ch1.envTimer    = m_ch1.nrx2 & 0x07u;
        if (m_ch1.envTimer == 0) m_ch1.envTimer = 8;

        // Sweep trigger - PanDocs Audio Details - Pulse channel with sweep
        uint8_t pace = (m_nr10 >> 4) & 0x07u;
        uint8_t step = m_nr10 & 0x07u;

        m_sweep.shadow  = m_ch1.period;
        m_sweep.timer   = (pace != 0) ? pace : 8;
        m_sweep.enabled = (pace != 0) || (step != 0);
        m_sweep.negUsed = false;

        // Immediate overflow check if step != 0
        if (step != 0)
        {
            uint8_t dir = (m_nr10 >> 3) & 1u;
            uint16_t newP = m_sweep.calcNewPeriod(step, dir);
            if (dir == 1) m_sweep.negUsed = true;
            if (newP > 0x7FFu)
                m_ch1.active = false;
        }

        if (!m_ch1.dacEnabled())
            m_ch1.active = false;
    }

    void APU::triggerCh2()
    {
        // PanDocs Audio Registers - NR24 trigger
        if (m_ch2.dacEnabled())
            m_ch2.active = true;

        if (m_ch2.lengthTimer >= 64)
            m_ch2.lengthTimer = 0;

        m_ch2.periodTimer = m_ch2.period;
        m_ch2.volume      = (m_ch2.nrx2 >> 4) & 0x0Fu;
        m_ch2.envTimer    = m_ch2.nrx2 & 0x07u;
        if (m_ch2.envTimer == 0) m_ch2.envTimer = 8;

        if (!m_ch2.dacEnabled())
            m_ch2.active = false;
    }

    void APU::triggerCh3()
    {
        // PanDocs Audio Registers - NR34 trigger
        if (!m_ch3.dacEnabled()) return;

        m_ch3.active = true;

        if (m_ch3.lengthTimer >= 256)
            m_ch3.lengthTimer = 0;

        m_ch3.periodTimer = m_ch3.period;
        m_ch3.sampleIndex = 0;
        // sampleBuffer NOT cleared - PanDocs Audio Details
    }

    void APU::triggerCh4()
    {
        // PanDocs Audio Registers - NR44 trigger
        if (m_ch4.dacEnabled())
            m_ch4.active = true;

        if (m_ch4.lengthTimer >= 64)
            m_ch4.lengthTimer = 0;

        m_ch4.lfsr      = 0x0000;
        m_ch4.freqTimer = m_ch4.calcPeriod();
        m_ch4.volume    = (m_ch4.nrx2 >> 4) & 0x0Fu;
        m_ch4.envTimer  = m_ch4.nrx2 & 0x07u;
        if (m_ch4.envTimer == 0) m_ch4.envTimer = 8;

        if (!m_ch4.dacEnabled())
            m_ch4.active = false;
    }

    // -- Power on/off ----------------------------------------------------

    void APU::powerOff()
    {
        // PanDocs Audio Registers - NR52 power-off
        // All registers 0xFF10–0xFF25 reset to 0x00.
        m_nr10 = 0x00;
        m_nr50 = 0x00;
        m_nr51 = 0x00;

        // DMG preserves length timers across power-off; clear everything else.
        uint8_t ch1Len = m_ch1.lengthTimer;
        uint8_t ch2Len = m_ch2.lengthTimer;
        uint16_t ch3Len = m_ch3.lengthTimer;
        uint8_t ch4Len = m_ch4.lengthTimer;

        m_ch1 = PulseChannel{};
        m_ch2 = PulseChannel{};
        m_ch3 = WaveChannel{};
        m_ch4 = NoiseChannel{};
        m_sweep = SweepUnit{};

        // Restore DMG length timers
        m_ch1.lengthTimer = ch1Len;
        m_ch2.lengthTimer = ch2Len;
        m_ch3.lengthTimer = ch3Len;
        m_ch4.lengthTimer = ch4Len;

        // Wave RAM is NOT cleared - PanDocs Audio Registers
        m_powered = false;
    }

    void APU::powerOn()
    {
        // PanDocs Audio Registers - NR52 power-on
        m_frameSeqStep = 0;
        m_ch3.sampleBuffer = 0; // sample buffer cleared on APU power-on
        m_powered = true;
    }

    // -- Sample generation -----------------------------------------------

    void APU::generateSample()
    {
        // PanDocs Audio Details - Mixer
        int ch1Out = m_ch1.output();
        int ch2Out = m_ch2.output();
        int ch3Out = m_ch3.output();
        int ch4Out = m_ch4.output();

        // Mix per NR51 panning - PanDocs Audio Registers - NR51
        int leftMix  = 0;
        int rightMix = 0;
        if (m_nr51 & 0x10u) leftMix  += ch1Out;
        if (m_nr51 & 0x20u) leftMix  += ch2Out;
        if (m_nr51 & 0x40u) leftMix  += ch3Out;
        if (m_nr51 & 0x80u) leftMix  += ch4Out;
        if (m_nr51 & 0x01u) rightMix += ch1Out;
        if (m_nr51 & 0x02u) rightMix += ch2Out;
        if (m_nr51 & 0x04u) rightMix += ch3Out;
        if (m_nr51 & 0x08u) rightMix += ch4Out;

        // Master volume - PanDocs Audio Registers - NR50
        int leftVol  = ((m_nr50 >> 4) & 0x07u) + 1;
        int rightVol = (m_nr50 & 0x07u) + 1;
        leftMix  *= leftVol;
        rightMix *= rightVol;

        // Normalize to [-1, 1]: max = 4 channels * 15 * 8 = 480
        float left  = static_cast<float>(leftMix)  / 480.0f;
        float right = static_cast<float>(rightMix) / 480.0f;

        // High-pass filter - PanDocs Audio Details - Mixer
        // Charge factor adjusted for 48 kHz sample rate: 0.999958^(4194304/48000) ≈ 0.9963
        constexpr double kChargeFactor = 0.9963;
        float outL = static_cast<float>(left  - m_hpfCapLeft);
        m_hpfCapLeft  = left  - outL * kChargeFactor;
        float outR = static_cast<float>(right - m_hpfCapRight);
        m_hpfCapRight = right - outR * kChargeFactor;

        // Push to ring buffer
        uint32_t nextWrite = (m_sampleWritePos + 2) % (SAMPLE_BUFFER_SIZE * 2);
        if (nextWrite != m_sampleReadPos) // don't overwrite unread data
        {
            m_sampleBuffer[m_sampleWritePos]     = outL;
            m_sampleBuffer[m_sampleWritePos + 1] = outR;
            m_sampleWritePos = nextWrite;
        }
    }

    uint32_t APU::drainSamples(float* outBuffer, uint32_t maxPairs)
    {
        uint32_t count = 0;
        while (count < maxPairs && m_sampleReadPos != m_sampleWritePos)
        {
            outBuffer[count * 2]     = m_sampleBuffer[m_sampleReadPos];
            outBuffer[count * 2 + 1] = m_sampleBuffer[m_sampleReadPos + 1];
            m_sampleReadPos = (m_sampleReadPos + 2) % (SAMPLE_BUFFER_SIZE * 2);
            ++count;
        }
        return count;
    }

    // -- Register read ---------------------------------------------------

    uint8_t APU::read(uint16_t addr) const
    {
        // Wave RAM - always accessible
        if (addr >= 0xFF30u && addr <= 0xFF3Fu)
        {
            if (m_ch3.active) return 0xFFu; // conservative: reads 0xFF while CH3 playing
            return m_waveRam[addr - 0xFF30u];
        }

        // NR52 - always readable
        if (addr == 0xFF26u)
        {
            uint8_t val = 0x70u; // bits 6–4 always 1
            if (m_powered) val |= 0x80u;
            if (m_ch1.active) val |= 0x01u;
            if (m_ch2.active) val |= 0x02u;
            if (m_ch3.active) val |= 0x04u;
            if (m_ch4.active) val |= 0x08u;
            return val;
        }

        // All other registers return 0xFF when APU is off
        if (!m_powered) return 0xFFu;

        if (addr < 0xFF10u || addr > 0xFF25u) return 0xFFu;

        uint8_t reg = 0x00;
        uint8_t mask = kReadMask[addr - 0xFF10u];

        switch (addr)
        {
            // CH1
            case 0xFF10u: reg = m_nr10; break;
            case 0xFF11u: reg = m_ch1.nrx1; break;
            case 0xFF12u: reg = m_ch1.nrx2; break;
            case 0xFF13u: reg = 0x00; break; // write-only
            case 0xFF14u: reg = m_ch1.lengthEnable ? 0x40u : 0x00u; break;

            // CH2
            case 0xFF15u: reg = 0x00; break; // unused
            case 0xFF16u: reg = m_ch2.nrx1; break;
            case 0xFF17u: reg = m_ch2.nrx2; break;
            case 0xFF18u: reg = 0x00; break; // write-only
            case 0xFF19u: reg = m_ch2.lengthEnable ? 0x40u : 0x00u; break;

            // CH3
            case 0xFF1Au: reg = m_ch3.nr30; break;
            case 0xFF1Bu: reg = 0x00; break; // write-only
            case 0xFF1Cu: reg = m_ch3.nr32; break;
            case 0xFF1Du: reg = 0x00; break; // write-only
            case 0xFF1Eu: reg = m_ch3.lengthEnable ? 0x40u : 0x00u; break;

            // CH4
            case 0xFF1Fu: reg = 0x00; break; // unused
            case 0xFF20u: reg = 0x00; break; // write-only
            case 0xFF21u: reg = m_ch4.nrx2; break;
            case 0xFF22u: reg = m_ch4.nr43; break;
            case 0xFF23u: reg = m_ch4.lengthEnable ? 0x40u : 0x00u; break;

            // Global
            case 0xFF24u: reg = m_nr50; break;
            case 0xFF25u: reg = m_nr51; break;
        }

        return reg | mask;
    }

    // -- Register write --------------------------------------------------

    void APU::write(uint16_t addr, uint8_t val)
    {
        // Wave RAM - always writable
        if (addr >= 0xFF30u && addr <= 0xFF3Fu)
        {
            if (m_ch3.active) return; // conservative: writes ignored while CH3 playing
            m_waveRam[addr - 0xFF30u] = val;
            return;
        }

        // NR52 - only bit 7 is writable
        if (addr == 0xFF26u)
        {
            bool newPower = (val >> 7) & 1;
            if (m_powered && !newPower)
                powerOff();
            else if (!m_powered && newPower)
                powerOn();
            return;
        }

        // All other writes silently ignored when APU is off
        if (!m_powered) return;

        switch (addr)
        {
            // -- CH1 -------------------------------------------------
            case 0xFF10u: // NR10 - sweep
            {
                uint8_t oldDir = (m_nr10 >> 3) & 1u;
                m_nr10 = val;
                uint8_t newDir = (val >> 3) & 1u;
                // Switching sub→add after neg was used disables CH1 - PanDocs Audio Details
                if (oldDir == 1 && newDir == 0 && m_sweep.negUsed)
                    m_ch1.active = false;
                break;
            }
            case 0xFF11u: // NR11 - duty + length
                m_ch1.nrx1 = val;
                m_ch1.lengthTimer = val & 0x3Fu;
                break;
            case 0xFF12u: // NR12 - volume envelope
                m_ch1.nrx2 = val;
                if (!m_ch1.dacEnabled())
                    m_ch1.active = false;
                break;
            case 0xFF13u: // NR13 - period low
                m_ch1.nrx3 = val;
                m_ch1.period = static_cast<uint16_t>((m_ch1.period & 0x700u) | val);
                break;
            case 0xFF14u: // NR14 - period high + control
            {
                bool oldLenEnable = m_ch1.lengthEnable;
                m_ch1.nrx4 = val;
                m_ch1.period = static_cast<uint16_t>((m_ch1.period & 0xFFu) | ((val & 0x07u) << 8));
                m_ch1.lengthEnable = (val >> 6) & 1u;

                // Extra length clocking - PanDocs Audio - Length timer obscure behavior
                if (!oldLenEnable && m_ch1.lengthEnable && (m_frameSeqStep & 1))
                {
                    if (m_ch1.lengthTimer < 64)
                    {
                        ++m_ch1.lengthTimer;
                        if (m_ch1.lengthTimer >= 64 && !((val >> 7) & 1u))
                            m_ch1.active = false;
                    }
                }

                if ((val >> 7) & 1u) triggerCh1();
                break;
            }

            // -- CH2 -------------------------------------------------
            case 0xFF15u: break; // unused
            case 0xFF16u: // NR21 - duty + length
                m_ch2.nrx1 = val;
                m_ch2.lengthTimer = val & 0x3Fu;
                break;
            case 0xFF17u: // NR22 - volume envelope
                m_ch2.nrx2 = val;
                if (!m_ch2.dacEnabled())
                    m_ch2.active = false;
                break;
            case 0xFF18u: // NR23 - period low
                m_ch2.nrx3 = val;
                m_ch2.period = static_cast<uint16_t>((m_ch2.period & 0x700u) | val);
                break;
            case 0xFF19u: // NR24 - period high + control
            {
                bool oldLenEnable = m_ch2.lengthEnable;
                m_ch2.nrx4 = val;
                m_ch2.period = static_cast<uint16_t>((m_ch2.period & 0xFFu) | ((val & 0x07u) << 8));
                m_ch2.lengthEnable = (val >> 6) & 1u;

                if (!oldLenEnable && m_ch2.lengthEnable && (m_frameSeqStep & 1))
                {
                    if (m_ch2.lengthTimer < 64)
                    {
                        ++m_ch2.lengthTimer;
                        if (m_ch2.lengthTimer >= 64 && !((val >> 7) & 1u))
                            m_ch2.active = false;
                    }
                }

                if ((val >> 7) & 1u) triggerCh2();
                break;
            }

            // -- CH3 -------------------------------------------------
            case 0xFF1Au: // NR30 - DAC enable
                m_ch3.nr30 = val;
                if (!m_ch3.dacEnabled())
                    m_ch3.active = false;
                break;
            case 0xFF1Bu: // NR31 - length
                m_ch3.lengthTimer = val; // 0–255, limit is 256
                break;
            case 0xFF1Cu: // NR32 - output level
                m_ch3.nr32 = val;
                break;
            case 0xFF1Du: // NR33 - period low
                m_ch3.nrx3 = val;
                m_ch3.period = static_cast<uint16_t>((m_ch3.period & 0x700u) | val);
                break;
            case 0xFF1Eu: // NR34 - period high + control
            {
                bool oldLenEnable = m_ch3.lengthEnable;
                m_ch3.nrx4 = val;
                m_ch3.period = static_cast<uint16_t>((m_ch3.period & 0xFFu) | ((val & 0x07u) << 8));
                m_ch3.lengthEnable = (val >> 6) & 1u;

                if (!oldLenEnable && m_ch3.lengthEnable && (m_frameSeqStep & 1))
                {
                    if (m_ch3.lengthTimer < 256)
                    {
                        ++m_ch3.lengthTimer;
                        if (m_ch3.lengthTimer >= 256 && !((val >> 7) & 1u))
                            m_ch3.active = false;
                    }
                }

                if ((val >> 7) & 1u) triggerCh3();
                break;
            }

            // -- CH4 -------------------------------------------------
            case 0xFF1Fu: break; // unused
            case 0xFF20u: // NR41 - length
                m_ch4.nrx1 = val;
                m_ch4.lengthTimer = val & 0x3Fu;
                break;
            case 0xFF21u: // NR42 - volume envelope
                m_ch4.nrx2 = val;
                if (!m_ch4.dacEnabled())
                    m_ch4.active = false;
                break;
            case 0xFF22u: // NR43 - frequency + LFSR width
                m_ch4.nr43 = val;
                break;
            case 0xFF23u: // NR44 - control
            {
                bool oldLenEnable = m_ch4.lengthEnable;
                m_ch4.nrx4 = val;
                m_ch4.lengthEnable = (val >> 6) & 1u;

                if (!oldLenEnable && m_ch4.lengthEnable && (m_frameSeqStep & 1))
                {
                    if (m_ch4.lengthTimer < 64)
                    {
                        ++m_ch4.lengthTimer;
                        if (m_ch4.lengthTimer >= 64 && !((val >> 7) & 1u))
                            m_ch4.active = false;
                    }
                }

                if ((val >> 7) & 1u) triggerCh4();
                break;
            }

            // -- Global ----------------------------------------------
            case 0xFF24u: m_nr50 = val; break;
            case 0xFF25u: m_nr51 = val; break;
        }
    }

} // namespace SeaBoy
