#include "MBC3.hpp"
#include "../core/SaveState.hpp"

#include <cstring>

namespace SeaBoy
{
    MBC3::MBC3(std::vector<uint8_t> rom)
        : Cartridge(std::move(rom))
    {
        // PanDocs 17.4 - RAM size from header byte 0x0149
        static constexpr uint32_t kRamSizes[] = {0, 0, 0x2000, 0x8000, 0x20000, 0x10000};
        uint8_t ramCode = m_rom.size() > 0x0149u ? m_rom[0x0149] : 0;
        uint32_t ramSize = (ramCode < 6) ? kRamSizes[ramCode] : 0x2000u;
        m_ram.resize(ramSize, 0x00);

        // Cartridge type 0x0F and 0x10 include RTC
        uint8_t type = m_rom.size() > 0x0147u ? m_rom[0x0147] : 0;
        m_hasRTC = (type == 0x0F || type == 0x10);
    }

    // PanDocs 17.4 MBC3 read routing
    uint8_t MBC3::read(uint16_t addr) const
    {
        // 0x0000-0x3FFF: ROM bank 0 (fixed)
        if (addr <= 0x3FFFu)
            return addr < m_rom.size() ? m_rom[addr] : 0xFFu;

        // 0x4000-0x7FFF: switchable ROM bank
        if (addr <= 0x7FFFu)
        {
#ifdef PICO_BUILD
            if (m_bankLoader)
            {
                uint8_t bank = m_romBank;
                if (m_numRomBanks > 0) bank &= static_cast<uint8_t>(m_numRomBanks - 1u);
                loadBank(bank);
                return m_bankCache[addr - 0x4000u];
            }
#endif
            uint32_t offset = static_cast<uint32_t>(m_romBank) * 0x4000u
                            + static_cast<uint32_t>(addr - 0x4000u);
            return offset < m_rom.size() ? m_rom[offset] : 0xFFu;
        }

        // 0xA000-0xBFFF: RAM bank or latched RTC register
        if (addr >= 0xA000u && addr <= 0xBFFFu)
        {
            if (!m_ramEnable)
                return 0xFFu;

            // RAM banks 0x00-0x03
            if (m_ramBank <= 0x03u)
            {
                if (m_ram.empty())
                    return 0xFFu;
                uint32_t offset = static_cast<uint32_t>(m_ramBank) * 0x2000u
                                + static_cast<uint32_t>(addr - 0xA000u);
                return offset < m_ram.size() ? m_ram[offset] : 0xFFu;
            }

            // RTC registers 0x08-0x0C (return latched values)
            if (m_hasRTC)
            {
                switch (m_ramBank)
                {
                    case 0x08: return m_latchS;
                    case 0x09: return m_latchM;
                    case 0x0A: return m_latchH;
                    case 0x0B: return m_latchDL;
                    case 0x0C: return m_latchDH;
                    default:   break;
                }
            }
            return 0xFFu;
        }

        return 0xFFu;
    }

    // PanDocs 17.4 MBC3 write routing
    void MBC3::write(uint16_t addr, uint8_t val)
    {
        // 0x0000-0x1FFF: RAM/RTC enable
        if (addr <= 0x1FFFu)
        {
            m_ramEnable = (val & 0x0Fu) == 0x0Au;
            return;
        }

        // 0x2000-0x3FFF: ROM bank (7-bit; 0 -> 1)
        if (addr <= 0x3FFFu)
        {
            m_romBank = val & 0x7Fu;
            if (m_romBank == 0) m_romBank = 1;
            return;
        }

        // 0x4000-0x5FFF: RAM bank / RTC register select
        if (addr <= 0x5FFFu)
        {
            m_ramBank = val;
            return;
        }

        // 0x6000-0x7FFF: Latch clock data
        if (addr <= 0x7FFFu)
        {
            // 0x00 -> 0x01 sequence triggers latch
            if (m_latchState == 0x00 && val == 0x01)
            {
                m_latchS  = m_rtcS;
                m_latchM  = m_rtcM;
                m_latchH  = m_rtcH;
                m_latchDL = m_rtcDL;
                m_latchDH = m_rtcDH;
            }
            m_latchState = val;
            return;
        }

        // 0xA000-0xBFFF: RAM bank write or live RTC register write
        if (addr >= 0xA000u && addr <= 0xBFFFu && m_ramEnable)
        {
            // RAM banks 0x00-0x03
            if (m_ramBank <= 0x03u && !m_ram.empty())
            {
                uint32_t offset = static_cast<uint32_t>(m_ramBank) * 0x2000u
                                + static_cast<uint32_t>(addr - 0xA000u);
                if (offset < m_ram.size())
                    m_ram[offset] = val;
                return;
            }

            // RTC registers 0x08-0x0C (write to live registers)
            if (m_hasRTC)
            {
                switch (m_ramBank)
                {
                    case 0x08: m_rtcS  = val; break;
                    case 0x09: m_rtcM  = val; break;
                    case 0x0A: m_rtcH  = val; break;
                    case 0x0B: m_rtcDL = val; break;
                    case 0x0C: m_rtcDH = val; break;
                    default:   break;
                }
            }
        }
    }

    void MBC3::serialize(BinaryWriter& w) const
    {
        w.write8(m_romBank);
        w.write8(m_ramBank);
        w.writeBool(m_ramEnable);
        // RTC live registers
        w.write8(m_rtcS);
        w.write8(m_rtcM);
        w.write8(m_rtcH);
        w.write8(m_rtcDL);
        w.write8(m_rtcDH);
        // RTC latched copies
        w.write8(m_latchS);
        w.write8(m_latchM);
        w.write8(m_latchH);
        w.write8(m_latchDL);
        w.write8(m_latchDH);
        w.write8(m_latchState);
        w.writeBool(m_hasRTC);
        // RAM
        w.write32(static_cast<uint32_t>(m_ram.size()));
        if (!m_ram.empty())
            w.writeBlock(m_ram.data(), m_ram.size());
    }

    void MBC3::deserialize(BinaryReader& r)
    {
        m_romBank   = r.read8();
        m_ramBank   = r.read8();
        m_ramEnable = r.readBool();
        m_rtcS      = r.read8();
        m_rtcM      = r.read8();
        m_rtcH      = r.read8();
        m_rtcDL     = r.read8();
        m_rtcDH     = r.read8();
        m_latchS    = r.read8();
        m_latchM    = r.read8();
        m_latchH    = r.read8();
        m_latchDL   = r.read8();
        m_latchDH   = r.read8();
        m_latchState = r.read8();
        m_hasRTC    = r.readBool();
        uint32_t ramSize = r.read32();
        m_ram.resize(ramSize);
        if (ramSize > 0)
            r.readBlock(m_ram.data(), ramSize);
    }

    void MBC3::loadSRAM(const uint8_t* data, size_t size)
    {
        // SRAM data, optionally followed by 5 RTC bytes
        size_t ramCopy = (size < m_ram.size()) ? size : m_ram.size();
        if (!m_ram.empty())
            std::memcpy(m_ram.data(), data, ramCopy);

        // If extra bytes present after SRAM, treat as RTC registers
        if (m_hasRTC && size > m_ram.size() + 4)
        {
            const uint8_t* rtc = data + m_ram.size();
            m_rtcS  = rtc[0];
            m_rtcM  = rtc[1];
            m_rtcH  = rtc[2];
            m_rtcDL = rtc[3];
            m_rtcDH = rtc[4];
        }
    }

}
