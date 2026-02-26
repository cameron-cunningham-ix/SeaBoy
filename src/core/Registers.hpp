#pragma once
#ifndef REGISTERS_H
#define REGISTERS_H

#include <cstdint>

// PanDocs.13 - CPU Registers
// Eight 8-bit general-purpose registers (A, F, B, C, D, E, H, L)
// Two 16-bit special registers (SP, PC)
// Register F (flags) lower 4 bits are always 0.
//
// Three distinct register group encodings used by the opcode decoder:
//   r8  (3-bit): B=0 C=1 D=2 E=3 H=4 L=5 (HL)=6 A=7  - 8-bit operands
//   rp  (2-bit): BC=0 DE=1 HL=2 SP=3                 - 16-bit loads/ALU
//   rp2 (2-bit): BC=0 DE=1 HL=2 AF=3                 - PUSH/POP only

namespace SeaBoy
{
    // Register F bit masks
    constexpr uint8_t FLAG_Z = 0x80; // Bit 7 - Zero
    constexpr uint8_t FLAG_N = 0x40; // Bit 6 - Subtract
    constexpr uint8_t FLAG_H = 0x20; // Bit 5 - Half-Carry
    constexpr uint8_t FLAG_C = 0x10; // Bit 4 - Carry

    struct Registers
    {
        uint8_t  A = 0, F = 0;
        uint8_t  B = 0, C = 0;
        uint8_t  D = 0, E = 0;
        uint8_t  H = 0, L = 0;
        uint16_t SP = 0;
        uint16_t PC = 0;

        // Flag accessors
        inline void setF(uint8_t v)   { F = v & 0xF0; }

        bool flagZ() const { return (F & FLAG_Z) != 0; }
        bool flagN() const { return (F & FLAG_N) != 0; }
        bool flagH() const { return (F & FLAG_H) != 0; }
        bool flagC() const { return (F & FLAG_C) != 0; }

        void setFlagZ(bool v) { v ? (F |= FLAG_Z) : (F &= ~FLAG_Z); }
        void setFlagN(bool v) { v ? (F |= FLAG_N) : (F &= ~FLAG_N); }
        void setFlagH(bool v) { v ? (F |= FLAG_H) : (F &= ~FLAG_H); }
        void setFlagC(bool v) { v ? (F |= FLAG_C) : (F &= ~FLAG_C); }

        // 16-bit pair accessors
        uint16_t getAF() const { return (static_cast<uint16_t>(A) << 8) | F; }
        void setAF(uint16_t v) { A = static_cast<uint8_t>(v >> 8); setF(static_cast<uint8_t>(v)); }

        uint16_t getBC() const { return (static_cast<uint16_t>(B) << 8) | C; }
        void setBC(uint16_t v) { B = static_cast<uint8_t>(v >> 8); C = static_cast<uint8_t>(v); }

        uint16_t getDE() const { return (static_cast<uint16_t>(D) << 8) | E; }
        void setDE(uint16_t v) { D = static_cast<uint8_t>(v >> 8); E = static_cast<uint8_t>(v); }

        uint16_t getHL() const { return (static_cast<uint16_t>(H) << 8) | L; }
        void setHL(uint16_t v) { H = static_cast<uint8_t>(v >> 8); L = static_cast<uint8_t>(v); }

        // r8 - 3-bit register field, used in most 8-bit instructions
        // PanDocs.14 - encoding: B=0 C=1 D=2 E=3 H=4 L=5 (HL)=6 A=7
        // Returns nullptr for idx==6; might have to change later
        uint8_t* r8Ptr(uint8_t idx)
        {
            switch (idx & 0x7)
            {
                case 0: return &B;
                case 1: return &C;
                case 2: return &D;
                case 3: return &E;
                case 4: return &H;
                case 5: return &L;
                case 6: return nullptr; // indirect via HL
                case 7: return &A;
                default: return nullptr;
            }
        }

        const uint8_t* r8Ptr(uint8_t idx) const
        {
            return const_cast<Registers*>(this)->r8Ptr(idx);
        }

        // rp - 2-bit register pair field, used in 16-bit loads/arithmetic
        // PanDocs.14 - encoding: BC=0 DE=1 HL=2 SP=3
        uint16_t getRP(uint8_t idx) const
        {
            switch (idx & 0x3)
            {
                case 0: return getBC();
                case 1: return getDE();
                case 2: return getHL();
                case 3: return SP;
                default: return 0;
            }
        }

        void setRP(uint8_t idx, uint16_t v)
        {
            switch (idx & 0x3)
            {
                case 0: setBC(v); break;
                case 1: setDE(v); break;
                case 2: setHL(v); break;
                case 3: SP = v;   break;
            }
        }

        // rp2 - 2-bit register pair field for PUSH/POP only
        // PanDocs.14 - encoding: BC=0 DE=1 HL=2 AF=3
        uint16_t getRP2(uint8_t idx) const
        {
            switch (idx & 0x3)
            {
                case 0: return getBC();
                case 1: return getDE();
                case 2: return getHL();
                case 3: return getAF();
                default: return 0;
            }
        }

        void setRP2(uint8_t idx, uint16_t v)
        {
            switch (idx & 0x3)
            {
                case 0: setBC(v); break;
                case 1: setDE(v); break;
                case 2: setHL(v); break;
                case 3: setAF(v); break; // setAF enforces F lower-nibble mask
            }
        }
    };

}

#endif
