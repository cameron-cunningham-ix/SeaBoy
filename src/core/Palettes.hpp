#pragma once

#include <cstdint>

// PanDocs.4.7 Palettes - DMG palette registers
// BGP  (0xFF47): Background palette data
// OBP0 (0xFF48): Object palette 0 data
// OBP1 (0xFF49): Object palette 1 data
//
// Each register maps 2-bit color IDs to shades:
//   bits 1:0 = color 0, bits 3:2 = color 1,
//   bits 5:4 = color 2, bits 7:6 = color 3
//
// TODO CGB color RAM (BCPS/BCPD/OCPS/OCPD)

namespace SeaBoy
{
    class Palettes
    {
    public:
        Palettes();

        void reset();

        // Register access
        uint8_t readBGP()  const { return m_bgp; }
        uint8_t readOBP0() const { return m_obp0; }
        uint8_t readOBP1() const { return m_obp1; }

        void writeBGP(uint8_t val)  { m_bgp  = val; }
        void writeOBP0(uint8_t val) { m_obp0 = val; }
        void writeOBP1(uint8_t val) { m_obp1 = val; }

        // Resolve a 2-bit color ID to an RGBA8888 value.
        // PanDocs.4.7 Palettes - shade mapping through palette register
        uint32_t resolveBG(uint8_t colorID) const;
        uint32_t resolveOBJ(uint8_t colorID, uint8_t paletteNum) const;

    private:
        // DMG shade table: white, light gray, dark gray, black (RGBA8888)
        static constexpr uint32_t s_shades[4] = {
            0xFFFFFFFF, // shade 0: white
            0xAAAAAAFF, // shade 1: light gray
            0x555555FF, // shade 2: dark gray
            0x000000FF  // shade 3: black
        };

        // Extract shade index for a given 2-bit colorID from a palette register
        static uint8_t shadeIndex(uint8_t palette, uint8_t colorID)
        {
            return (palette >> (colorID * 2)) & 0x03;
        }

        uint8_t m_bgp  = 0xFC; // PanDocs.22 Power Up Sequence
        uint8_t m_obp0 = 0x00;
        uint8_t m_obp1 = 0x00;
    };
}