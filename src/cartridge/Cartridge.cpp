#include "Cartridge.hpp"
#include "MBC0.hpp"
#include "MBC1.hpp"

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

            default:
                std::fprintf(stderr,
                    "Cartridge: unsupported MBC type 0x%02X - falling back to MBC0\n", type);
                return std::make_unique<MBC0>(std::move(rom));
        }
    }

}
