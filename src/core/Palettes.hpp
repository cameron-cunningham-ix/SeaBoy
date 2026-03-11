#pragma once

#include <cstdint>

#include "SaveState.hpp"

// PanDocs.4.7 Palettes - DMG palette registers
// BGP  (0xFF47): Background palette data
// OBP0 (0xFF48): Object palette 0 data
// OBP1 (0xFF49): Object palette 1 data
//
// Each register maps 2-bit color IDs to shades:
//   bits 1:0 = color 0, bits 3:2 = color 1,
//   bits 5:4 = color 2, bits 7:6 = color 3
//
// CGB color RAM: 8 BG palettes + 8 OBJ palettes, each 4 colors × 2 bytes = 64 bytes
// Accessed via auto-increment data ports:
//   BCPS/BCPD (0xFF68/69): BG palette index + data
//   OCPS/OCPD (0xFF6A/6B): OBJ palette index + data

namespace SeaBoy
{
    class Palettes
    {
    public:
        Palettes();

        void reset();

        // --- DMG palette registers ---
        uint8_t readBGP()  const { return m_bgp; }
        uint8_t readOBP0() const { return m_obp0; }
        uint8_t readOBP1() const { return m_obp1; }

        void writeBGP(uint8_t val)  { m_bgp  = val; }
        void writeOBP0(uint8_t val) { m_obp0 = val; }
        void writeOBP1(uint8_t val) { m_obp1 = val; }

        // Resolve a 2-bit color ID to an RGBA8888 value (DMG).
        // PanDocs.4.7 Palettes - shade mapping through palette register
        uint32_t resolveBG(uint8_t colorID) const;
        uint32_t resolveOBJ(uint8_t colorID, uint8_t paletteNum) const;

        // --- CGB color RAM registers - PanDocs.4.7 CGB Palettes ---
        uint8_t readBCPS() const { return m_bcps; }
        uint8_t readBCPD() const { return m_bcram[m_bcps & 0x3Fu]; }
        uint8_t readOCPS() const { return m_ocps; }
        uint8_t readOCPD() const { return m_ocram[m_ocps & 0x3Fu]; }

        void writeBCPS(uint8_t val) { m_bcps = val; }
        void writeBCPD(uint8_t val);
        void writeOCPS(uint8_t val) { m_ocps = val; }
        void writeOCPD(uint8_t val);

        // Resolve CGB palette color to RGBA8888.
        // paletteIdx: 0-7, colorID: 0-3
        uint32_t resolveBGCGB(uint8_t paletteIdx, uint8_t colorID) const;
        uint32_t resolveOBJCGB(uint8_t paletteIdx, uint8_t colorID) const;

        // Set custom DMG shade colors (RGBA8888, 4 entries: white→black)
        void setShades(const uint32_t shades[4])
        {
            for (int i = 0; i < 4; ++i) m_shades[i] = shades[i];
        }
        const uint32_t* shades() const { return m_shades; }

        // Save state serialization
        void serialize(BinaryWriter& w) const;
        void deserialize(BinaryReader& r);

    private:
        // DMG shade table: white, light gray, dark gray, black (RGBA8888)
        // Mutable so the UI can swap in custom palettes at runtime.
        uint32_t m_shades[4] = {
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

        // Convert CGB 15-bit RGB to RGBA8888
        static uint32_t cgbColorToRGBA(uint8_t lo, uint8_t hi);

        // DMG palette registers
        uint8_t m_bgp  = 0xFC; // PanDocs.22 Power Up Sequence
        uint8_t m_obp0 = 0x00;
        uint8_t m_obp1 = 0x00;

        // CGB color RAM - 64 bytes each (8 palettes × 4 colors × 2 bytes)
        uint8_t m_bcram[64]{};  // BG palette RAM
        uint8_t m_ocram[64]{};  // OBJ palette RAM
        uint8_t m_bcps = 0;     // BG palette spec (index + auto-increment in bit 7)
        uint8_t m_ocps = 0;     // OBJ palette spec
    };
}