#pragma once

#include "Cartridge.hpp"

namespace SeaBoy
{
    // MBC5 - ROM banking up to 8 MB (9-bit bank), RAM up to 128 KB (16 banks).
    // PanDocs.17.5 MBC5
    //
    // Key difference from MBC1: bank 0 IS valid (no 0->1 remap). 9-bit ROM bank.
    //
    // Register map (write-only, decoded from ROM address bus):
    //   0x0000–0x1FFF  RAM enable  (lower nibble 0x0A = enable)
    //   0x2000–0x2FFF  ROM bank low 8 bits
    //   0x3000–0x3FFF  ROM bank bit 8
    //   0x4000–0x5FFF  RAM bank (4-bit, 0–15)
    class MBC5 final : public Cartridge
    {
    public:
        explicit MBC5(std::vector<uint8_t> rom);

        uint8_t read(uint16_t addr) const override;
        void    write(uint16_t addr, uint8_t val) override;

        void serialize(BinaryWriter& w) const override;
        void deserialize(BinaryReader& r) override;

        const uint8_t* sram() const override { return m_ram.data(); }
        size_t sramSize() const override { return m_ram.size(); }
        void loadSRAM(const uint8_t* data, size_t size) override;

    private:
        // PanDocs.17.5 - up to 16 banks × 8 KB = 128 KB external RAM
        std::vector<uint8_t> m_ram;

        uint16_t m_romBank   = 1;     // 9-bit ROM bank (0–511); bank 0 is valid
        uint8_t  m_ramBank   = 0;     // 4-bit RAM bank (0–15)
        bool     m_ramEnable = false;
    };

}
