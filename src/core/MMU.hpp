#pragma once
#ifndef MMU_H
#define MMU_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// PanDocs.2 - Memory Map
// This is a minimal stub just to unblock CPU implementation for now
//
// Stub address space:
//   0x0000–0x7FFF  ROM (32 KB, bank 0+1)
//   0xC000–0xDFFF  WRAM (8 KB)
//   0xFF0F         IF - Interrupt Flag
//   0xFF80–0xFFFE  HRAM (127 bytes)
//   0xFFFF         IE - Interrupt Enable
//   all other addresses → open bus, reads return 0xFF

namespace SeaBoy
{
    // Named address constants
    constexpr uint16_t ADDR_ROM_BANK  = 0x3FFF;
    constexpr uint16_t ADDR_ROM_END   = 0x7FFF;
    constexpr uint16_t ADDR_WRAM_BASE = 0xC000;
    constexpr uint16_t ADDR_WRAM_END  = 0xDFFF;
    constexpr uint16_t ADDR_IF        = 0xFF0F;
    constexpr uint16_t ADDR_HRAM_BASE = 0xFF80;
    constexpr uint16_t ADDR_HRAM_END  = 0xFFFE;
    constexpr uint16_t ADDR_IE        = 0xFFFF;

    class MMU
    {
    public:
        MMU();

        void reset();
        void loadROM(const uint8_t* data, size_t size);

        // Core bus interface
        // Each call conceptually represents a 4 T-cycle bus access,
        // cycle accounting is performed by the CPU, not here.
        uint8_t read8(uint16_t addr) const;
        void write8(uint16_t addr, uint8_t val);

        // 16-bit helpers - always little-endian (low byte at addr, high at addr+1)
        // Implemented via two read8/write8 calls so future access gating applies.
        // PanDocs - LD (nn),SP and PUSH/POP byte order
        uint16_t read16(uint16_t addr) const;
        void write16(uint16_t addr, uint16_t val);

        // Interrupt register helpers to avoid scattering 0xFF0F / 0xFFFF
        uint8_t readIF() const;
        void writeIF(uint8_t v);
        uint8_t readIE() const;
        void writeIE(uint8_t v);

        // Serial port output (captured from writes to SB/SC, 0xFF01/02).
        // Blargg test ROMs write results here. Safe to call at any time.
        const std::string& serialOutput() const { return m_serialOutput; }

        // Test mode: flat 64 KB overlay, bypasses all normal routing.
        // Used by sm83_runner for single-step CPU tests.
        void enableTestMode();
        bool inTestMode() const { return m_testRam != nullptr; }
        void    testWrite(uint16_t addr, uint8_t val) { m_testRam[addr] = val; }
        uint8_t testRead (uint16_t addr) const       { return m_testRam[addr]; }

    private:
        // ROM: variable-length; bank 0 = [0x0000,0x3FFF], bank N = [N*0x4000, N*0x4000+0x3FFF]
        std::vector<uint8_t> m_rom;
        uint8_t m_mbcType    = 0;  // Cartridge type (header byte 0x0147)
        uint8_t m_romBankNum = 1;  // Active ROM bank for 0x4000–0x7FFF (MBC1)

        uint8_t m_wram[0x2000]{};  // 8 KB WRAM
        uint8_t m_hram[0x7F]{};    // 127 bytes HRAM (0xFF80–0xFFFE)
        uint8_t m_ifReg = 0xE1;    // IF - power-on value per PanDocs §Power_Up_Sequence
        uint8_t m_ie    = 0x00;    // IE

        // Serial port – PanDocs.7 Serial Data Transfer
        uint8_t     m_sb = 0;      // 0xFF01 SB: transfer data
        uint8_t     m_sc = 0;      // 0xFF02 SC: transfer control
        std::string m_serialOutput;// Accumulated bytes captured from serial transfers

        // Non-null only when test mode is active (heap-allocated to avoid 64 KB stack cost)
        std::unique_ptr<uint8_t[]> m_testRam;
    };

}

#endif
