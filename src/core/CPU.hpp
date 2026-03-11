#pragma once
#ifndef CPU_H
#define CPU_H

#include <array>
#include <cstdint>

#include "Registers.hpp"
#include "SaveState.hpp"

// PanDocs.14 & GBCTR - Sharp SM83 CPU
// Cycle unit: T-cycles (4 T-cycles per M-cycle).
// step() returns T-cycles consumed by the instruction executed.

namespace SeaBoy
{
    class MMU;

    class CPU
    {
    public:
        explicit CPU(MMU& mmu);

        // Reset registers to post-boot (DMG) values.
        // cgb=true uses CGB register set.
        // headerChecksum is ROM header byte 0x014D; affects initial F on DMG.
        // PanDocs Power Up Sequence
        void reset(bool cgb = false, uint8_t headerChecksum = 0x00);

        // Execute one instruction (or one HALT idle cycle).
        // Returns T-cycles consumed, including any interrupt dispatch overhead.
        uint32_t step();

        // Read-only access to registers for debugging / tests
        const Registers& registers() const { return m_regs; }

        // ---- Accessors used by opcode handler functions (Opcodes.cpp / OpcodesCB.cpp) ----

        Registers& regs()  { return m_regs; }
        MMU&       mmu()   { return m_mmu;  }

        bool ime()          const { return m_ime; }
        bool halted()       const { return m_halted; }
        bool haltBug()      const { return m_haltBug; }
        bool imeScheduled() const { return m_imeScheduled; }

        void setIME(bool v)          { m_ime          = v; }
        void setHalted(bool v)       { m_halted       = v; }
        void setHaltBug(bool v)      { m_haltBug      = v; }
        void setIMEScheduled(bool v) { m_imeScheduled = v; }

        // Save state serialization
        void serialize(BinaryWriter& w) const;
        void deserialize(BinaryReader& r);

        // Fetch one byte at PC and advance PC (used by handlers for immediate operands)
        uint8_t  fetch8();
        uint16_t fetch16();

        // Tick subsystems for an internal M-cycle (4 T-cycles, no bus access).
        // Used by opcode handlers with internal cycles (INC rr, PUSH, JP, CALL, RET, etc.)
        void internalCycle();

        // Dispatch a CB-prefix sub-opcode. Called by op_CB in Opcodes.cpp.
        uint32_t dispatchCB(uint8_t subOpcode) { return kCBOpcodes[subOpcode](*this); }

    private:
        Registers m_regs;
        MMU&      m_mmu;

        bool m_ime          = false; // Interrupt Master Enable
        bool m_halted       = false; // HALT state - PanDocs.9.2
        bool m_haltBug      = false; // HALT bug pending - PanDocs.9.2
        bool m_imeScheduled = false; // EI delay - PanDocs.9

        // Check and service pending interrupts.
        // Returns 20 T-cycles if an interrupt was dispatched, 0 otherwise.
        uint32_t handleInterrupts();

        // Opcode dispatch tables - defined in CPU.cpp, entries populated by
        // Opcodes.cpp (unprefixed) and OpcodesCB.cpp (CB-prefixed).
        using OpcodeHandler = uint32_t(*)(CPU&);
        static const std::array<OpcodeHandler, 256> kOpcodes;
        static const std::array<OpcodeHandler, 256> kCBOpcodes;
    };

}

#endif
