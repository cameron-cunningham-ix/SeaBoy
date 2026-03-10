#include "Cartridge.hpp"
#include "MBC0.hpp"
#include "MBC1.hpp"
#include "MBC2.hpp"
#include "MBC3.hpp"
#include "MBC5.hpp"

#include <cstdio>

namespace SeaBoy
{
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
