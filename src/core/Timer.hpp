#pragma once

#include <cstdint>

// PanDocs.8 Timer and Divider Registers
// https://gbdev.io/pandocs/Timer_and_Divider_Registers.html
//
// The Timer is driven by a 16-bit internal counter that increments every T-cycle.
//   DIV  (0xFF04) = upper 8 bits of the counter  -> increments at ~16384 Hz
//   TIMA (0xFF05) = timer counter; overflows trigger an interrupt
//   TMA  (0xFF06) = timer modulo; reloaded into TIMA 4 T-cycles after overflow
//   TAC  (0xFF07) = timer control (bit 2 = enable, bits 1:0 = clock select)
//
// TIMA is driven by a falling-edge detector on one bit of the internal counter,
// selected by TAC[1:0]:
//   00 -> bit 9   (4096  Hz)
//   01 -> bit 3   (262144 Hz)
//   10 -> bit 5   (65536 Hz)
//   11 -> bit 7   (16384 Hz)
//
// Overflow quirk (PanDocs.8.1 Timer obscure behaviour):
//   When TIMA overflows it stays at 0x00 for exactly 4 T-cycles.
//   After the delay: TIMA ← TMA and IF bit 2 is set.
//   Writing to TIMA during those 4 T-cycles cancels the scheduled reload.

namespace SeaBoy
{
    class MMU; // forward declaration - MMU.hpp included in Timer.cpp

    class Timer
    {
    public:
        explicit Timer(MMU& mmu);

        // Reset to power-on state (counter = 0, TIMA/TMA/TAC = 0).
        void reset();

        // Advance timer by tCycles T-cycles.
        // Sets IF bit 2 (Timer interrupt) on TIMA overflow after the 4-cycle delay.
        void tick(uint32_t tCycles);

        // I/O register interface - called by MMU for 0xFF04–0xFF07.
        uint8_t read(uint16_t addr) const;
        void    write(uint16_t addr, uint8_t val);

    private:
        // Returns the internal counter bit index selected by TAC[1:0]
        // PanDocs.8 Timer and Divider Registers (clock select table)
        uint8_t selectedBit() const;

        MMU&     m_mmu;
        uint16_t m_counter       = 0; // 16-bit internal counter; DIV = m_counter >> 8
        uint8_t  m_tima          = 0; // 0xFF05
        uint8_t  m_tma           = 0; // 0xFF06
        uint8_t  m_tac           = 0; // 0xFF07 (timer stopped, clock select = 0)
        int      m_overflowDelay = 0; // counts down 4->0; at 0 triggers TMA reload + interrupt
    };

}
