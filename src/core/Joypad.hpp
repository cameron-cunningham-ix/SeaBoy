#pragma once
#ifndef JOYPAD_H
#define JOYPAD_H

#include <cstdint>

#include "SaveState.hpp"

// PanDocs.6 Joypad Input (0xFF00 P1/JOYP)
// Active-low button matrix: 0 = pressed, 1 = released.
// Bit 5 selects the action button group (A / B / Select / Start).
// Bit 4 selects the d-pad group (Right / Left / Up / Down).
// Bits 6-7 are unused and always read 1.

namespace SeaBoy
{
    enum class Button : uint8_t
    {
        A      = 0,
        B      = 1,
        Select = 2,
        Start  = 3,
        Right  = 4,
        Left   = 5,
        Up     = 6,
        Down   = 7,
    };

    class Joypad
    {
    public:
        // Called by GameBoy to wire joypad interrupt (IF bit 4) on button press.
        using IFCallback = void(*)(void* ctx);
        void setIFCallback(IFCallback cb, void* ctx) { m_ifCb = cb; m_ifCtx = ctx; }

        // Called by UIPlatform (via GameBoy::setButton) on each SDL key event.
        void setButton(Button btn, bool pressed);

        // P1/JOYP register read - returns current combined state.
        uint8_t read() const;

        // P1/JOYP register write - stores select bits (4-5) only; bits 0-3 are read-only.
        void write(uint8_t v);

        // Save state serialization
        void serialize(BinaryWriter& w) const;
        void deserialize(BinaryReader& r);

    private:
        // Bits 5-4: select lines (0 = group selected). Default: neither group selected.
        uint8_t m_select  = 0x30u;
        // Active-low nibbles: bit N = 0 means button pressed.
        // Action: bit0=A, bit1=B, bit2=Select, bit3=Start
        uint8_t m_action  = 0x0Fu;
        // D-pad:  bit0=Right, bit1=Left, bit2=Up, bit3=Down
        uint8_t m_dpad    = 0x0Fu;

        // Previous combined low nibble - used for edge detection.
        uint8_t m_prevLow = 0x0Fu;

        IFCallback m_ifCb  = nullptr;
        void*      m_ifCtx = nullptr;

        // Returns the combined low nibble for the currently-selected group(s).
        uint8_t lowNibble() const;

        // Fires the IF callback if any output line transitioned high->low (button press).
        void checkEdge();
    };

}

#endif
