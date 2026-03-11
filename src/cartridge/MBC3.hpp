#pragma once

#include "Cartridge.hpp"

namespace SeaBoy
{
    // MBC3 - ROM banking up to 2 MB, optional RAM up to 32 KB, optional RTC.
    // PanDocs §17.4 MBC3
    //
    // Register map (write-only, decoded from ROM address bus):
    //   0x0000–0x1FFF  RAM/RTC enable  (lower nibble 0x0A = enable)
    //   0x2000–0x3FFF  ROM bank number (7-bit; 0 -> 1)
    //   0x4000–0x5FFF  RAM bank / RTC register select
    //   0x6000–0x7FFF  Latch clock data (0x00 -> 0x01 sequence latches RTC)
    //
    // RAM/RTC read/write at 0xA000–0xBFFF:
    //   ramBank 0x00–0x03 → RAM bank
    //   ramBank 0x08–0x0C → RTC register (latched on read, live on write)
    class MBC3 final : public Cartridge
    {
    public:
        explicit MBC3(std::vector<uint8_t> rom);

        uint8_t read(uint16_t addr) const override;
        void    write(uint16_t addr, uint8_t val) override;

        void serialize(BinaryWriter& w) const override;
        void deserialize(BinaryReader& r) override;

        const uint8_t* sram() const override { return m_ram.data(); }
        size_t sramSize() const override { return m_ram.size(); }
        void loadSRAM(const uint8_t* data, size_t size) override;

    private:
        // PanDocs §17.4 - up to 4 banks × 8 KB = 32 KB external RAM
        std::vector<uint8_t> m_ram;

        uint8_t m_romBank   = 1;     // 7-bit ROM bank; 0 remapped to 1
        uint8_t m_ramBank   = 0;     // 0x00–0x03 = RAM banks, 0x08–0x0C = RTC regs
        bool    m_ramEnable = false;

        // RTC live registers
        uint8_t m_rtcS  = 0;        // Seconds (0–59)
        uint8_t m_rtcM  = 0;        // Minutes (0–59)
        uint8_t m_rtcH  = 0;        // Hours   (0–23)
        uint8_t m_rtcDL = 0;        // Day counter lower 8 bits
        uint8_t m_rtcDH = 0;        // Day counter upper 1 bit + carry + halt

        // RTC latched copies (snapshot when latch triggered)
        uint8_t m_latchS  = 0;
        uint8_t m_latchM  = 0;
        uint8_t m_latchH  = 0;
        uint8_t m_latchDL = 0;
        uint8_t m_latchDH = 0;

        // Latch state machine: writing 0x00 then 0x01 triggers latch
        uint8_t m_latchState = 0xFF;

        // Whether this cartridge has an RTC (type 0x0F or 0x10)
        bool m_hasRTC = false;
    };

}
