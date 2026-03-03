#include "Timer.hpp"
#include "MMU.hpp"

namespace SeaBoy
{
    Timer::Timer(MMU& mmu)
        : m_mmu(mmu)
    {
        reset();
    }

    void Timer::reset()
    {
        m_counter       = 0;
        m_tima          = 0;
        m_tma           = 0;
        m_tac           = 0;
        m_overflowDelay = 0;
    }

    // PanDocs.8.1 Timer obscure behaviour - bit tap table
    // TAC[1:0] : 00->bit 9, 01->bit 3, 10->bit 5, 11->bit 7
    uint8_t Timer::selectedBit() const
    {
        static constexpr uint8_t kBitPos[4] = {9u, 3u, 5u, 7u};
        return kBitPos[m_tac & 0x03u];
    }

    // PanDocs.8 Timer and Divider Registers & 8.1 Timer obscure behaviour
    void Timer::tick(uint32_t tCycles)
    {
        const bool enable = (m_tac & 0x04u) != 0u;
        const uint8_t bit = selectedBit();

        for (uint32_t i = 0; i < tCycles; ++i)
        {
            // 1. Overflow delay countdown (must run even if timer disabled - PanDocs)
            if (m_overflowDelay > 0)
            {
                if (--m_overflowDelay == 0)
                {
                    m_tima = m_tma;
                    // Set Timer interrupt (IF bit 2) - PanDocs.9.1 INT $50 Timer interrupt
                    m_mmu.writeIF(m_mmu.readIF() | 0x04u);
                }
            }

            // 2. Advance internal counter (always runs, including when timer is stopped)
            const uint16_t old = m_counter;
            ++m_counter;

            // 3. TIMA increments on falling edge of (selected_counter_bit AND timer_enable)
            const bool oldBit = enable && ((old       >> bit) & 1u) != 0u;
            const bool newBit = enable && ((m_counter >> bit) & 1u) != 0u;
            if (oldBit && !newBit)
            {
                if (++m_tima == 0u)
                    m_overflowDelay = 4; // 4 T-cycle delay before TMA reload
            }
        }
    }

    uint8_t Timer::read(uint16_t addr) const
    {
        switch (addr)
        {
            case 0xFF04u: return static_cast<uint8_t>(m_counter >> 8); // DIV
            case 0xFF05u: return m_tima;
            case 0xFF06u: return m_tma;
            case 0xFF07u: return m_tac | 0xF8u; // upper 5 bits read as 1 (open bus)
            default:      return 0xFFu;
        }
    }

    void Timer::write(uint16_t addr, uint8_t val)
    {
        switch (addr)
        {
            case 0xFF04u: // DIV: any write resets the internal counter
            {
                // If the selected counter bit is currently set, resetting the counter
                // creates a falling edge -> spurious TIMA tick (PanDocs.8.1 Timer obscure)
                const bool enable = (m_tac & 0x04u) != 0u;
                const bool wasSet = enable && ((m_counter >> selectedBit()) & 1u) != 0u;
                m_counter = 0u;
                if (wasSet)
                {
                    if (++m_tima == 0u)
                        m_overflowDelay = 4;
                }
                break;
            }

            case 0xFF05u: // TIMA: write during overflow delay cancels scheduled reload
                m_overflowDelay = 0;
                m_tima = val;
                break;

            case 0xFF06u: // TMA
                m_tma = val;
                break;

            case 0xFF07u: // TAC
            {
                // Changing TAC may create a falling edge if the old selected bit was set
                // and the new effective signal (enable AND new_bit) is cleared.
                const bool oldEnable = (m_tac & 0x04u) != 0u;
                const bool oldBit    = oldEnable && ((m_counter >> selectedBit()) & 1u) != 0u;

                m_tac = val & 0x07u; // only lower 3 bits are writable

                const bool newEnable = (m_tac & 0x04u) != 0u;
                const bool newBit    = newEnable && ((m_counter >> selectedBit()) & 1u) != 0u;

                if (oldBit && !newBit)
                {
                    if (++m_tima == 0u)
                        m_overflowDelay = 4;
                }
                break;
            }

            default:
                break;
        }
    }

}
