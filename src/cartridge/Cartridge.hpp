#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#ifdef PICO_BUILD
#include <functional>
#endif

namespace SeaBoy
{
    struct BinaryWriter;
    struct BinaryReader;

    // PanDocs.16 - Cartridge abstract interface.
    // Covers the cartridge address regions:
    //   0x0000-0x7FFF  ROM (bank 0 fixed + switchable bank)
    //   0xA000-0xBFFF  External RAM (if present)
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

        // Cartridge type byte from header byte 0x0147.
        uint8_t typeCode() const {
            return (m_rom.size() > 0x0147u) ? m_rom[0x0147] : 0x00;
        }

        // ROM data access (for save state ROM hash verification).
        const uint8_t* romData() const { return m_rom.data(); }
        size_t romSize() const { return m_rom.size(); }

        // Serialization - save/load MBC banking state + external RAM.
        virtual void serialize(BinaryWriter& w) const = 0;
        virtual void deserialize(BinaryReader& r) = 0;

        // Battery-backed SRAM access for save files.
        virtual const uint8_t* sram() const { return nullptr; }
        virtual size_t sramSize() const { return 0; }
        virtual void loadSRAM(const uint8_t* data, size_t size) { (void)data; (void)size; }

        // Human-readable name for a cartridge type byte (0x0147). PanDocs.16
        static const char* typeString(uint8_t code);

        // Returns true if the type code includes battery-backed SRAM. PanDocs.16
        static bool hasBattery(uint8_t code);

        // Factory: parse header byte 0x0147, return the correct MBC subclass.
        // Takes ownership of the ROM data vector.
        static std::unique_ptr<Cartridge> create(std::vector<uint8_t> rom);

#ifdef PICO_BUILD
        // Bank streaming for large ROMs that don't fit in SRAM.
        //
        // On Pico, the ROM vector passed to create() may contain only bank 0 (16 KB).
        // When a BankLoader is installed, MBC switchable-bank reads (0x4000-0x7FFF) are
        // served from a 16 KB cache that is populated on-demand by calling this function.
        //
        // The callback receives a 16 KB buffer and the bank number to load, and must
        // fill the buffer with the 16 KB block at offset (bankNum * 0x4000) in the ROM.
        using BankLoader = std::function<void(uint8_t* buf, uint16_t bankNum)>;
        void setBankLoader(BankLoader loader);
        bool hasBankLoader() const { return bool(m_bankLoader); }
#endif

    protected:
        // Constructor is non-inline so Cartridge.cpp can initialise m_numRomBanks.
        explicit Cartridge(std::vector<uint8_t> rom);

        std::vector<uint8_t> m_rom;

        // Total number of 16 KB ROM banks derived from header byte 0x0148.
        // Used by streaming MBC reads to mask the bank number correctly.
        uint16_t m_numRomBanks = 0;

#ifdef PICO_BUILD
        // 16 KB cache for the current switchable bank (0x4000-0x7FFF).
        // Declared mutable so const read() can populate it on demand.
        mutable uint8_t  m_bankCache[0x4000]{};
        mutable uint16_t m_cachedBank = 0xFFFFu; // 0xFFFF = invalid / not yet loaded
        BankLoader m_bankLoader;

        // Load bankNum into m_bankCache if it is not already cached.
        void loadBank(uint16_t bankNum) const;
#endif
    };

}
