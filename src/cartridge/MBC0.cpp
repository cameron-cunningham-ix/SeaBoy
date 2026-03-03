#include "MBC0.hpp"

namespace SeaBoy
{
    MBC0::MBC0(std::vector<uint8_t> rom)
        : Cartridge(std::move(rom))
    {}

    // PanDocs.16.1 - flat 32 KB ROM, address maps directly to ROM offset.
    uint8_t MBC0::read(uint16_t addr) const
    {
        if (addr < m_rom.size())
            return m_rom[addr];
        return 0xFFu;
    }

    // ROM-only: all writes are silently ignored.
    void MBC0::write(uint16_t /*addr*/, uint8_t /*val*/) {}

}
