#include "MMU.hpp"

#include <algorithm>
#include <cstring>

namespace SeaBoy
{
    MMU::MMU()
    {
        reset();
    }

    void MMU::reset()
    {
        // ROM is not cleared here – it is replaced wholesale by loadROM().
        m_mbcType    = 0;
        m_romBankNum = 1;

        std::memset(m_wram, 0x00, sizeof(m_wram));
        std::memset(m_hram, 0x00, sizeof(m_hram));
        m_ifReg = 0xE1; // PanDocs Power Up Sequence
        m_ie    = 0x00;

        m_sb = 0;
        m_sc = 0;
        m_serialOutput.clear();
    }

    void MMU::loadROM(const uint8_t* data, size_t size)
    {
        m_rom.assign(data, data + size);

        // Parse cartridge type from header - PanDocs.16
        m_mbcType    = size > 0x0147u ? data[0x0147] : 0;
        m_romBankNum = 1; // reset to bank 1
    }

    void MMU::enableTestMode()
    {
        m_testRam = std::make_unique<uint8_t[]>(65536);
        std::memset(m_testRam.get(), 0x00, 65536);
        m_ifReg = 0; // no pending interrupts; IF must not alias test RAM at 0xFF0F
    }

    // PanDocs.2 - Memory Map routing
    uint8_t MMU::read8(uint16_t addr) const
    {
        if (m_testRam)
            return m_testRam[addr];

        // ROM bank 0 – always mapped, no MBC switching – PanDocs.17.2 MBC1
        if (addr <= 0x3FFFu)
        {
            return addr < m_rom.size() ? m_rom[addr] : 0xFFu;
        }

        // ROM bank N – fixed bank 1 for MBC0, switchable for MBC1 – PanDocs.17.2 MBC1
        if (addr <= ADDR_ROM_END) // 0x7FFF
        {
            uint32_t offset = static_cast<uint32_t>(m_romBankNum) * 0x4000u
                            + static_cast<uint32_t>(addr - 0x4000u);
            return offset < m_rom.size() ? m_rom[offset] : 0xFFu;
        }

        if (addr >= ADDR_WRAM_BASE && addr <= ADDR_WRAM_END)
        {
            return m_wram[addr - ADDR_WRAM_BASE];
        }

        // I/O registers (0xFF00–0xFF7F)
        if (addr == ADDR_IF)  return m_ifReg | 0xE0u; // upper 3 bits always 1
        if (addr == 0xFF01u)  return m_sb;
        if (addr == 0xFF02u)  return m_sc | 0x7Eu;    // unused bits read as 1

        // Minimal LCD stubs so Blargg ROMs don't spin waiting for VBlank
        // PanDocs.4.4 LCDC, STAT, LY
        if (addr == 0xFF40u)  return 0x91u; // LCDC: LCD on, BG enabled
        if (addr == 0xFF41u)  return 0x01u; // STAT: mode 1 (VBlank) — never blocks
        if (addr == 0xFF44u)  return 0x90u; // LY = 144 (first VBlank line)

        if (addr >= ADDR_HRAM_BASE && addr <= ADDR_HRAM_END)
        {
            return m_hram[addr - ADDR_HRAM_BASE];
        }
        if (addr == ADDR_IE)
        {
            return m_ie;
        }

        // Open bus - unmapped regions return 0xFF
        return 0xFFu;
    }

    void MMU::write8(uint16_t addr, uint8_t val)
    {
        if (m_testRam)
        {
            m_testRam[addr] = val;
            return;
        }

        // ROM area: MBC register writes – PanDocs.17.2 MBC1
        if (addr <= ADDR_ROM_END) // 0x7FFF
        {
            // MBC1: ROM bank number written to 0x2000–0x3FFF (lower 5 bits)
            // MBC type 0x01/0x02/0x03 = MBC1 (no RAM / with RAM / with RAM+battery)
            if (m_mbcType >= 0x01u && m_mbcType <= 0x03u &&
                addr >= 0x2000u && addr <= 0x3FFFu)
            {
                m_romBankNum = val & 0x1Fu;
                if (m_romBankNum == 0) m_romBankNum = 1; // 0x00 → bank 1 – PanDocs.17.2 MBC1
            }
            // 0x0000–0x1FFF (RAM enable) and 0x4000–0x7FFF (bank set hi / mode) ignored for now
            return;
        }

        if (addr >= ADDR_WRAM_BASE && addr <= ADDR_WRAM_END)
        {
            m_wram[addr - ADDR_WRAM_BASE] = val;
            return;
        }

        if (addr == ADDR_IF)
        {
            m_ifReg = val & 0x1Fu; // only lower 5 bits are writable
            return;
        }

        // Serial port – PanDocs.7 Serial Data Transfer
        if (addr == 0xFF01u) { m_sb = val; return; }
        if (addr == 0xFF02u)
        {
            m_sc = val;
            if (val & 0x80u) // Transfer start (internal clock)
            {
                m_serialOutput += static_cast<char>(m_sb);
                m_sc &= ~0x80u; // Clear start bit – transfer completes "instantly"
                // TODO: set IF bit 3 (serial interrupt) if needed
            }
            return;
        }

        if (addr >= ADDR_HRAM_BASE && addr <= ADDR_HRAM_END)
        {
            m_hram[addr - ADDR_HRAM_BASE] = val;
            return;
        }
        if (addr == ADDR_IE)
        {
            m_ie = val;
            return;
        }
        // Writes to unmapped regions are silently ignored
    }

    uint16_t MMU::read16(uint16_t addr) const
    {
        // Little-endian: low byte at addr, high byte at addr+1
        uint8_t lo = read8(addr);
        uint8_t hi = read8(static_cast<uint16_t>(addr + 1));
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    void MMU::write16(uint16_t addr, uint16_t val)
    {
        // Little-endian: low byte first
        write8(addr,                          static_cast<uint8_t>(val & 0xFF));
        write8(static_cast<uint16_t>(addr + 1), static_cast<uint8_t>(val >> 8));
    }

    uint8_t MMU::readIF() const
    {
        // In test mode 0xFF0F is just program/data RAM; keep IF in m_ifReg so that
        // instruction bytes placed there don't cause spurious interrupt dispatch.
        if (m_testRam)
            return m_ifReg | 0xE0u;
        return read8(ADDR_IF);
    }

    void MMU::writeIF(uint8_t v)
    {
        if (m_testRam)
        {
            m_ifReg = v & 0x1Fu;
            return;
        }
        write8(ADDR_IF, v);
    }

    uint8_t MMU::readIE() const
    {
        if (m_testRam)
            return m_testRam[ADDR_IE];
        return m_ie;
    }

    void MMU::writeIE(uint8_t v)
    {
        if (m_testRam)
        {
            m_testRam[ADDR_IE] = v;
            return;
        }
        m_ie = v;
    }

}
