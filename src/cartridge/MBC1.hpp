#pragma once

#include "Cartridge.hpp"

namespace SeaBoy
{
    // MBC1 - ROM banking up to 2 MB, optional external RAM up to 32 KB.
    // PanDocs.17.2 MBC1
    //
    // Register map (write-only, decoded from ROM address bus):
    //   0x0000–0x1FFF  RAM enable  (0x0A in lower nibble = enable)
    //   0x2000–0x3FFF  ROM bank number (lower 5 bits; 0 -> 1)
    //   0x4000–0x5FFF  RAM bank / upper ROM bank bits (2 bits)
    //   0x6000–0x7FFF  Banking mode (0 = ROM, 1 = RAM)
    class MBC1 final : public Cartridge
    {
    public:
        explicit MBC1(std::vector<uint8_t> rom);

        uint8_t read(uint16_t addr) const override;
        void    write(uint16_t addr, uint8_t val) override;

    private:
        // PanDocs.17.2 - up to 4 banks × 8 KB = 32 KB external RAM
        std::vector<uint8_t> m_ram;

        uint8_t m_romBank   = 1;     // Lower 5 bits of ROM bank; 0 remapped to 1
        uint8_t m_ramBank   = 0;     // 2-bit RAM bank / upper ROM bank selector
        bool    m_ramEnable = false; // Enabled when 0x0A written to 0x0000–0x1FFF
        bool    m_mode      = false; // false = ROM banking mode, true = RAM banking mode
    };

}
