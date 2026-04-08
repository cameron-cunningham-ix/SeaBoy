#include "Cartridge.hpp"
#include "MBC0.hpp"
#include "MBC1.hpp"
#include "MBC2.hpp"
#include "MBC3.hpp"
#include "MBC5.hpp"

#include <cstdio>

namespace SeaBoy
{
    // ---------------------------------------------------------------------------
    // Cartridge base constructor
    // ---------------------------------------------------------------------------

    Cartridge::Cartridge(std::vector<uint8_t> rom)
        : m_rom(std::move(rom))
    {
        // Determine total bank count from ROM header byte 0x0148.
        // This is needed for streaming builds where m_rom may be truncated to 16 KB.
        // PanDocs.16 - ROM Size
        static constexpr uint32_t kBankCounts[] = {2,4,8,16,32,64,128,256,512};
        if (m_rom.size() > 0x0148u)
        {
            uint8_t code = m_rom[0x0148];
            m_numRomBanks = (code < 9)
                ? static_cast<uint16_t>(kBankCounts[code])
                : static_cast<uint16_t>(m_rom.size() / 0x4000u);
        }
        else
        {
            m_numRomBanks = static_cast<uint16_t>(m_rom.size() / 0x4000u);
        }
    }

#ifdef PICO_BUILD
    void Cartridge::setBankLoader(BankLoader loader)
    {
        m_bankLoader  = std::move(loader);
        m_cachedBank  = 0xFFFFu; // invalidate cache so next read triggers a load
    }

    void Cartridge::loadBank(uint16_t bankNum) const
    {
        if (!m_bankLoader || m_cachedBank == bankNum) return;
        m_bankLoader(m_bankCache, bankNum);
        m_cachedBank = bankNum;
    }
#endif

    // PanDocs.16 - Cartridge type byte 0x0147 name mapping.
    const char* Cartridge::typeString(uint8_t code)
    {
        switch (code)
        {
            case 0x00: return "ROM ONLY";
            case 0x01: return "MBC1";
            case 0x02: return "MBC1+RAM";
            case 0x03: return "MBC1+RAM+BATTERY";
            case 0x05: return "MBC2";
            case 0x06: return "MBC2+BATTERY";
            case 0x08: return "ROM+RAM";
            case 0x09: return "ROM+RAM+BATTERY";
            case 0x0B: return "MMM01";
            case 0x0C: return "MMM01+RAM";
            case 0x0D: return "MMM01+RAM+BATTERY";
            case 0x0F: return "MBC3+TIMER+BATTERY";
            case 0x10: return "MBC3+TIMER+RAM+BATTERY";
            case 0x11: return "MBC3";
            case 0x12: return "MBC3+RAM";
            case 0x13: return "MBC3+RAM+BATTERY";
            case 0x19: return "MBC5";
            case 0x1A: return "MBC5+RAM";
            case 0x1B: return "MBC5+RAM+BATTERY";
            case 0x1C: return "MBC5+RUMBLE";
            case 0x1D: return "MBC5+RUMBLE+RAM";
            case 0x1E: return "MBC5+RUMBLE+RAM+BATTERY";
            case 0x20: return "MBC6";
            case 0x22: return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
            case 0xFC: return "POCKET CAMERA";
            case 0xFD: return "BANDAI TAMA5";
            case 0xFE: return "HuC3";
            case 0xFF: return "HuC1+RAM+BATTERY";
            default:   return "UNKNOWN";
        }
    }

    // PanDocs.16 - Battery-backed type codes.
    bool Cartridge::hasBattery(uint8_t code)
    {
        switch (code)
        {
            case 0x03: case 0x06: case 0x09: case 0x0D:
            case 0x0F: case 0x10: case 0x13:
            case 0x1B: case 0x1E: case 0x22: case 0xFF:
                return true;
            default:
                return false;
        }
    }

    // PanDocs.16 - Cartridge type byte at 0x0147 selects the MBC.
    std::unique_ptr<Cartridge> Cartridge::create(std::vector<uint8_t> rom)
    {
        if (rom.size() < 0x0150u)
        {
            std::fprintf(stderr, "Cartridge: ROM too small (%zu bytes), treating as MBC0\n",
                         rom.size());
            return std::make_unique<MBC0>(std::move(rom));
        }

        const uint8_t type = rom[0x0147];

        switch (type)
        {
            case 0x00:                          // ROM ONLY
                return std::make_unique<MBC0>(std::move(rom));

            case 0x01:                          // MBC1
            case 0x02:                          // MBC1+RAM
            case 0x03:                          // MBC1+RAM+BATTERY
                return std::make_unique<MBC1>(std::move(rom));

            case 0x05:                          // MBC2
            case 0x06:                          // MBC2+BATTERY
                return std::make_unique<MBC2>(std::move(rom));

            case 0x0F:                          // MBC3+TIMER+BATTERY
            case 0x10:                          // MBC3+TIMER+RAM+BATTERY
            case 0x11:                          // MBC3
            case 0x12:                          // MBC3+RAM
            case 0x13:                          // MBC3+RAM+BATTERY
                return std::make_unique<MBC3>(std::move(rom));

            case 0x19:                          // MBC5
            case 0x1A:                          // MBC5+RAM
            case 0x1B:                          // MBC5+RAM+BATTERY
            case 0x1C:                          // MBC5+RUMBLE
            case 0x1D:                          // MBC5+RUMBLE+RAM
            case 0x1E:                          // MBC5+RUMBLE+RAM+BATTERY
                return std::make_unique<MBC5>(std::move(rom));

            default:
                std::fprintf(stderr,
                    "Cartridge: unsupported MBC type 0x%02X - falling back to MBC0\n", type);
                return std::make_unique<MBC0>(std::move(rom));
        }
    }

}
