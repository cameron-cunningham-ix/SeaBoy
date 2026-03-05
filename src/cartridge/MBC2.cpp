#include "MBC2.hpp"

#include <cstring>

namespace SeaBoy
{
    MBC2::MBC2(std::vector<uint8_t> rom)
        : Cartridge(std::move(rom))
    {
        std::memset(m_ram, 0x00, sizeof(m_ram));
    }

    // PanDocs.17.3 MBC2 read routing
    uint8_t MBC2::read(uint16_t addr) const
    {
        // 0x0000–0x3FFF: ROM bank 0 (fixed)
        if (addr <= 0x3FFFu)
            return addr < m_rom.size() ? m_rom[addr] : 0xFFu;

        // 0x4000–0x7FFF: switchable ROM bank 1–15
        if (addr <= 0x7FFFu)
        {
            uint32_t offset = static_cast<uint32_t>(m_romBank) * 0x4000u
                            + static_cast<uint32_t>(addr - 0x4000u);
            return offset < m_rom.size() ? m_rom[offset] : 0xFFu;
        }

        // 0xA000–0xBFFF: 512-nibble internal RAM (mirrored)
        if (addr >= 0xA000u && addr <= 0xBFFFu)
        {
            if (!m_ramEnable)
                return 0xFFu;
            // 512-byte window mirrored every 512 bytes within the 8 KB region
            uint16_t offset = (addr - 0xA000u) & 0x01FFu;
            // Upper nibble always 0xF (PanDocs.17.3 MBC2)
            return 0xF0u | (m_ram[offset] & 0x0Fu);
        }

        return 0xFFu;
    }

    // PanDocs.17.3 MBC2 write routing
    void MBC2::write(uint16_t addr, uint8_t val)
    {
        // 0x0000–0x3FFF: RAM enable or ROM bank select, gated by address bit 8
        if (addr <= 0x3FFFu)
        {
            if (addr & 0x0100u)
            {
                // Bit 8 set -> ROM bank register (lower 4 bits; 0 -> 1)
                m_romBank = val & 0x0Fu;
                if (m_romBank == 0) m_romBank = 1;
            }
            else
            {
                // Bit 8 clear -> RAM enable (lower nibble 0x0A = enable)
                m_ramEnable = (val & 0x0Fu) == 0x0Au;
            }
            return;
        }

        // 0xA000–0xBFFF: internal RAM writes (nibble only)
        if (addr >= 0xA000u && addr <= 0xBFFFu && m_ramEnable)
        {
            uint16_t offset = (addr - 0xA000u) & 0x01FFu;
            m_ram[offset] = val & 0x0Fu;  // store only lower nibble
        }
    }

}
