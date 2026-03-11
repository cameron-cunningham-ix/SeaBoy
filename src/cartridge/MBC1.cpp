#include "MBC1.hpp"
#include "../core/SaveState.hpp"

#include <cstring>

namespace SeaBoy
{
    MBC1::MBC1(std::vector<uint8_t> rom)
        : Cartridge(std::move(rom))
    {
        // PanDocs.17.2 - RAM size from header byte 0x0149:
        //   0x00=0, 0x01=unused, 0x02=8KB, 0x03=32KB, 0x04=128KB, 0x05=64KB
        static constexpr uint32_t kRamSizes[] = {0, 0, 0x2000, 0x8000, 0x20000, 0x10000};
        uint8_t ramCode = m_rom.size() > 0x0149u ? m_rom[0x0149] : 0;
        uint32_t ramSize = (ramCode < 6) ? kRamSizes[ramCode] : 0x2000u;
        m_ram.resize(ramSize, 0x00);
    }

    // PanDocs.17.2 MBC1 read routing
    uint8_t MBC1::read(uint16_t addr) const
    {
        // 0x0000–0x3FFF: ROM bank 0 (always fixed)
        // In mode 1 the upper 2 bits can remap this window too, but only for >512 KB ROMs.
        // Blargg cpu_instrs is 64 KB, so mode 1 bank-0 remapping is not needed yet.
        if (addr <= 0x3FFFu)
        {
            return addr < m_rom.size() ? m_rom[addr] : 0xFFu;
        }

        // 0x4000–0x7FFF: switchable ROM bank
        if (addr <= 0x7FFFu)
        {
            // Full bank index: upper 2 bits from m_ramBank (mode 0 only), lower 5 from m_romBank.
            uint8_t  bank   = static_cast<uint8_t>((m_ramBank << 5u) | (m_romBank & 0x1Fu));
            uint32_t offset = static_cast<uint32_t>(bank) * 0x4000u
                            + static_cast<uint32_t>(addr - 0x4000u);
            return offset < m_rom.size() ? m_rom[offset] : 0xFFu;
        }

        // 0xA000–0xBFFF: external RAM
        if (addr >= 0xA000u && addr <= 0xBFFFu && m_ramEnable && !m_ram.empty())
        {
            uint8_t  bank   = m_mode ? m_ramBank : 0;
            uint32_t offset = static_cast<uint32_t>(bank) * 0x2000u
                            + static_cast<uint32_t>(addr - 0xA000u);
            return offset < m_ram.size() ? m_ram[offset] : 0xFFu;
        }
        return 0xFFu;
    }

    // PanDocs.17.2 MBC1 write routing (MBC register updates)
    void MBC1::write(uint16_t addr, uint8_t val)
    {
        if (addr <= 0x1FFFu)
        {
            // RAM enable: lower nibble 0x0A = enable, anything else = disable
            m_ramEnable = (val & 0x0Fu) == 0x0Au;
        }
        else if (addr <= 0x3FFFu)
        {
            // ROM bank number (lower 5 bits); bank 0 -> 1 (PanDocs.17.2)
            m_romBank = val & 0x1Fu;
            if (m_romBank == 0) m_romBank = 1;
        }
        else if (addr <= 0x5FFFu)
        {
            // RAM bank / upper ROM bank bits (2 bits)
            m_ramBank = val & 0x03u;
        }
        else if (addr <= 0x7FFFu)
        {
            // Banking mode select
            m_mode = (val & 0x01u) != 0u;
        }
        // 0xA000–0xBFFF: external RAM writes
        if (addr >= 0xA000u && addr <= 0xBFFFu && m_ramEnable && !m_ram.empty())
        {
            uint8_t  bank   = m_mode ? m_ramBank : 0;
            uint32_t offset = static_cast<uint32_t>(bank) * 0x2000u
                            + static_cast<uint32_t>(addr - 0xA000u);
            if (offset < m_ram.size())
                m_ram[offset] = val;
        }
    }

    void MBC1::serialize(BinaryWriter& w) const
    {
        w.write8(m_romBank);
        w.write8(m_ramBank);
        w.writeBool(m_ramEnable);
        w.writeBool(m_mode);
        w.write32(static_cast<uint32_t>(m_ram.size()));
        if (!m_ram.empty())
            w.writeBlock(m_ram.data(), m_ram.size());
    }

    void MBC1::deserialize(BinaryReader& r)
    {
        m_romBank   = r.read8();
        m_ramBank   = r.read8();
        m_ramEnable = r.readBool();
        m_mode      = r.readBool();
        uint32_t ramSize = r.read32();
        m_ram.resize(ramSize);
        if (ramSize > 0)
            r.readBlock(m_ram.data(), ramSize);
    }

    void MBC1::loadSRAM(const uint8_t* data, size_t size)
    {
        size_t copySize = (size < m_ram.size()) ? size : m_ram.size();
        std::memcpy(m_ram.data(), data, copySize);
    }

}
