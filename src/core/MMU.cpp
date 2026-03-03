#include "MMU.hpp"

#include "../cartridge/Cartridge.hpp"
#include "Timer.hpp"

#include <algorithm>
#include <cstring>

namespace SeaBoy
{
    MMU::MMU()
    {
        reset();
    }

    // Defined here so unique_ptr<Cartridge> is destructed where Cartridge is fully visible.
    MMU::~MMU() = default;

    void MMU::reset()
    {
        // Cartridge is not reset here - it is replaced wholesale by loadROM().
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
        std::vector<uint8_t> romData(data, data + size);
        m_cart = Cartridge::create(std::move(romData));
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

        // ROM (bank 0 + switchable bank) and external RAM - delegated to Cartridge
        if (addr <= ADDR_ROM_END)
            return m_cart ? m_cart->read(addr) : 0xFFu;

        // 0xA000–0xBFFF: external RAM (cartridge)
        if (addr >= ADDR_ERAM_BASE && addr <= ADDR_ERAM_END)
            return m_cart ? m_cart->read(addr) : 0xFFu;

        if (addr >= ADDR_WRAM_BASE && addr <= ADDR_WRAM_END)
            return m_wram[addr - ADDR_WRAM_BASE];

        // I/O registers (0xFF00–0xFF7F)
        if (addr == ADDR_IF)  return m_ifReg | 0xE0u; // upper 3 bits always 1
        if (addr == 0xFF01u)  return m_sb;
        if (addr == 0xFF02u)  return m_sc | 0x7Eu;    // unused bits read as 1

        // Timer registers - PanDocs §Timer and Divider Registers
        if (addr >= ADDR_DIV && addr <= ADDR_TAC)
            return m_timer ? m_timer->read(addr) : 0xFFu;

        // Minimal LCD stubs so Blargg ROMs don't spin waiting for VBlank
        // PanDocs.4.4 LCDC, STAT, LY
        if (addr == 0xFF40u)  return 0x91u; // LCDC: LCD on, BG enabled
        if (addr == 0xFF41u)  return 0x01u; // STAT: mode 1 (VBlank) - never blocks
        if (addr == 0xFF44u)  return 0x90u; // LY = 144 (first VBlank line)

        if (addr >= ADDR_HRAM_BASE && addr <= ADDR_HRAM_END)
            return m_hram[addr - ADDR_HRAM_BASE];

        if (addr == ADDR_IE)
            return m_ie;

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

        // ROM area: MBC register writes (bank switching etc.) - delegated to Cartridge
        if (addr <= ADDR_ROM_END)
        {
            if (m_cart) m_cart->write(addr, val);
            return;
        }

        // 0xA000–0xBFFF: external RAM writes - delegated to Cartridge
        if (addr >= ADDR_ERAM_BASE && addr <= ADDR_ERAM_END)
        {
            if (m_cart) m_cart->write(addr, val);
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

        // Timer registers - PanDocs.8 Timer and Divider Registers
        if (addr >= ADDR_DIV && addr <= ADDR_TAC)
        {
            if (m_timer) m_timer->write(addr, val);
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
        write8(addr, static_cast<uint8_t>(val & 0xFF));
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
