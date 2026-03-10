#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace SeaBoy
{
    // PanDocs.16 - Cartridge abstract interface.
    // Covers the cartridge address regions:
    //   0x0000–0x7FFF  ROM (bank 0 fixed + switchable bank)
    //   0xA000–0xBFFF  External RAM (if present)
    // All other address regions are handled by the MMU directly.
    class Cartridge
    {
    public:
        virtual ~Cartridge() = default;

        // Read one byte from cartridge address space.
        // Returns 0xFF for unmapped/disabled regions.
        virtual uint8_t read(uint16_t addr) const = 0;

        // Write one byte to cartridge address space (MBC registers or external RAM).
        virtual void write(uint16_t addr, uint8_t val) = 0;

        // CGB flag from header byte 0x0143.
        // 0x80 = CGB compatible, 0xC0 = CGB only, other = DMG only.
        // PanDocs.10
        uint8_t cgbFlag() const {
            return (m_rom.size() > 0x0143u) ? m_rom[0x0143] : 0x00;
        }

        // Factory: parse header byte 0x0147, return the correct MBC subclass.
        // Takes ownership of the ROM data vector.
        static std::unique_ptr<Cartridge> create(std::vector<uint8_t> rom);

    protected:
        explicit Cartridge(std::vector<uint8_t> rom) : m_rom(std::move(rom)) {}

        std::vector<uint8_t> m_rom;
    };

}
