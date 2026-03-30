#pragma once

#include "Cartridge.hpp"

namespace SeaBoy
{
    // MBC2 - 16 ROM banks × 16 KB, 512 × 4-bit internal RAM.
    // PanDocs.17.3 MBC2
    //
    // Register map (write to 0x0000-0x3FFF):
    //   Bit 8 of address = 0  -> RAM enable (lower nibble 0x0A = enable)
    //   Bit 8 of address = 1  -> ROM bank (lower 4 bits; 0 -> 1)
    //
    // RAM:  512 nibbles at 0xA000-0xA1FF (mirrored across 0xA000-0xBFFF)
    //       Upper nibble always reads 0xF; only lower 4 bits are stored.
    class MBC2 final : public Cartridge
    {
    public:
        explicit MBC2(std::vector<uint8_t> rom);

        uint8_t read(uint16_t addr) const override;
        void    write(uint16_t addr, uint8_t val) override;

        void serialize(BinaryWriter& w) const override;
        void deserialize(BinaryReader& r) override;

        const uint8_t* sram() const override { return m_ram; }
        size_t sramSize() const override { return 512; }
        void loadSRAM(const uint8_t* data, size_t size) override;

    private:
        // PanDocs.17.3 MBC2 - 512 × 4-bit internal RAM (stored as full bytes, upper nibble masked)
        uint8_t m_ram[512]{};

        uint8_t m_romBank   = 1;     // 4-bit bank register; 0 remapped to 1
        bool    m_ramEnable = false; // Enabled when lower nibble 0x0A written with addr bit 8 = 0
    };

}
