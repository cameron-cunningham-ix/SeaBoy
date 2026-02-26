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
        std::memset(m_rom,  0xFF, sizeof(m_rom));
        std::memset(m_wram, 0x00, sizeof(m_wram));
        std::memset(m_hram, 0x00, sizeof(m_hram));
        m_ifReg = 0xE1; // PanDocs Power Up Sequence
        m_ie    = 0x00;
    }

    void MMU::loadROM(const uint8_t* data, size_t size)
    {
        size_t copySize = std::min(size, sizeof(m_rom));
        std::memcpy(m_rom, data, copySize);
    }

    // PanDocs.2 - Memory Map routing
    uint8_t MMU::read8(uint16_t addr) const
    {
        if (addr <= ADDR_ROM_END)
        {
            return m_rom[addr];
        }
        if (addr >= ADDR_WRAM_BASE && addr <= ADDR_WRAM_END)
        {
            return m_wram[addr - ADDR_WRAM_BASE];
        }
        if (addr == ADDR_IF)
        {
            return m_ifReg | 0xE0; // upper 3 bits always read as 1
        }
        if (addr >= ADDR_HRAM_BASE && addr <= ADDR_HRAM_END)
        {
            return m_hram[addr - ADDR_HRAM_BASE];
        }
        if (addr == ADDR_IE)
        {
            return m_ie;
        }

        // Open bus - unmapped regions return 0xFF
        return 0xFF;
    }

    void MMU::write8(uint16_t addr, uint8_t val)
    {
        if (addr <= ADDR_ROM_END)
        {
            // TODO: ROM writes are silently ignored in stub (MBC not yet implemented)
            return;
        }
        if (addr >= ADDR_WRAM_BASE && addr <= ADDR_WRAM_END)
        {
            m_wram[addr - ADDR_WRAM_BASE] = val;
            return;
        }
        if (addr == ADDR_IF)
        {
            m_ifReg = val & 0x1F; // only lower 5 bits are writable
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
        // TODO: Writes to unmapped regions are silently ignored
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
        return read8(ADDR_IF);
    }

    void MMU::writeIF(uint8_t v)
    {
        write8(ADDR_IF, v);
    }

    uint8_t MMU::readIE() const
    {
        return m_ie;
    }

    void MMU::writeIE(uint8_t v)
    {
        m_ie = v;
    }

}
