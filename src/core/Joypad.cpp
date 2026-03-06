#include "Joypad.hpp"

// PanDocs.6 Joypad Input
// Bit 5 low -> action group (A/B/Select/Start) exposed on bits 0-3.
// Bit 4 low -> d-pad group (Right/Left/Up/Down) exposed on bits 0-3.
// Both low -> each bit is the AND of both groups (0/pressed wins).
// Both high -> no group selected; bits 0-3 read 0xF (all released).
// Interrupt: fires on any high-to-low transition on P10-P13 (button pressed).

namespace SeaBoy
{
    uint8_t Joypad::lowNibble() const
    {
        uint8_t lo = 0x0Fu;
        if (!(m_select & 0x20u)) lo &= m_action; // bit 5 = 0: action group selected
        if (!(m_select & 0x10u)) lo &= m_dpad;   // bit 4 = 0: d-pad group selected
        return lo & 0x0Fu;
    }

    uint8_t Joypad::read() const
    {
        // Bits 7-6 always 1; bits 5-4 echo written select; bits 3-0 active-low state.
        return 0xC0u | (m_select & 0x30u) | lowNibble();
    }

    void Joypad::write(uint8_t v)
    {
        m_select = v & 0x30u;
        // Changing select lines may expose previously-pressed buttons -> check interrupt.
        checkEdge();
    }

    void Joypad::setButton(Button btn, bool pressed)
    {
        uint8_t bit = static_cast<uint8_t>(btn);
        if (bit <= 3u)
        {
            // Action group: A(0), B(1), Select(2), Start(3)
            if (pressed)
                m_action &= ~(1u << bit); // active-low: clear bit when pressed
            else
                m_action |= (1u << bit);  // set bit when released
        }
        else
        {
            // D-pad group: Right(4->bit0), Left(5->bit1), Up(6->bit2), Down(7->bit3)
            uint8_t dbit = bit - 4u;
            if (pressed)
                m_dpad &= ~(1u << dbit);
            else
                m_dpad |= (1u << dbit);
        }
        checkEdge();
    }

    void Joypad::checkEdge()
    {
        // PanDocs Joypad Input:
        // "An interrupt is requested when any of the bits 0-3 of the P1 register
        //  changes from high to low" (high-to-low = button pressed = active-low).
        uint8_t newLow = lowNibble();
        if ((m_prevLow & ~newLow) != 0u && m_ifCb)
            m_ifCb(m_ifCtx);
        m_prevLow = newLow;
    }

}
