#include "MBC5.hpp"

namespace SeaBoy
{
    MBC5::MBC5(std::vector<uint8_t> rom)
        : Cartridge(std::move(rom))
    {
        // PanDocs.17.5 - RAM size from header byte 0x0149
        static constexpr uint32_t kRamSizes[] = {0, 0, 0x2000, 0x8000, 0x20000, 0x10000};
        uint8_t ramCode = m_rom.size() > 0x0149u ? m_rom[0x0149] : 0;
        uint32_t ramSize = (ramCode < 6) ? kRamSizes[ramCode] : 0x2000u;
        m_ram.resize(ramSize, 0x00);
    }

    // PanDocs.17.5 MBC5 read routing
    uint8_t MBC5::read(uint16_t addr) const
    {
        // 0x0000–0x3FFF: ROM bank 0 (fixed)
        if (addr <= 0x3FFFu)
            return addr < m_rom.size() ? m_rom[addr] : 0xFFu;

        // 0x4000–0x7FFF: switchable ROM bank (9-bit)
        if (addr <= 0x7FFFu)
        {
            uint32_t offset = static_cast<uint32_t>(m_romBank) * 0x4000u
                            + static_cast<uint32_t>(addr - 0x4000u);
            return offset < m_rom.size() ? m_rom[offset] : 0xFFu;
        }

        // 0xA000–0xBFFF: RAM bank
        if (addr >= 0xA000u && addr <= 0xBFFFu)
        {
            if (!m_ramEnable || m_ram.empty())
                return 0xFFu;
            uint32_t offset = static_cast<uint32_t>(m_ramBank) * 0x2000u
                            + static_cast<uint32_t>(addr - 0xA000u);
            return offset < m_ram.size() ? m_ram[offset] : 0xFFu;
        }

        return 0xFFu;
    }

    // PanDocs.17.5 MBC5 write routing
    void MBC5::write(uint16_t addr, uint8_t val)
    {
        // 0x0000–0x1FFF: RAM enable
        if (addr <= 0x1FFFu)
        {
            m_ramEnable = (val & 0x0Fu) == 0x0Au;
            return;
        }

        // 0x2000–0x2FFF: ROM bank low 8 bits
        if (addr <= 0x2FFFu)
        {
            m_romBank = (m_romBank & 0x100u) | val;
            return;
        }

        // 0x3000–0x3FFF: ROM bank bit 8
        if (addr <= 0x3FFFu)
        {
            m_romBank = (m_romBank & 0xFFu) | (static_cast<uint16_t>(val & 0x01u) << 8);
            return;
        }

        // 0x4000–0x5FFF: RAM bank (4-bit)
        if (addr <= 0x5FFFu)
        {
            m_ramBank = val & 0x0Fu;
            return;
        }

        // 0xA000–0xBFFF: RAM write
        if (addr >= 0xA000u && addr <= 0xBFFFu && m_ramEnable && !m_ram.empty())
        {
            uint32_t offset = static_cast<uint32_t>(m_ramBank) * 0x2000u
                            + static_cast<uint32_t>(addr - 0xA000u);
            if (offset < m_ram.size())
                m_ram[offset] = val;
        }
    }

}
