#pragma once

#include "Cartridge.hpp"

namespace SeaBoy
{
    // MBC0 - ROM-only, no bank switching, no external RAM.
    // PanDocs.16.1 "MBC-less (ROM only)"
    // Supports up to 32 KB ROM (two fixed 16 KB banks, always bank 0 and 1).
    class MBC0 final : public Cartridge
    {
    public:
        explicit MBC0(std::vector<uint8_t> rom);

        uint8_t read(uint16_t addr) const override;
        void    write(uint16_t addr, uint8_t val) override;
    };

}
