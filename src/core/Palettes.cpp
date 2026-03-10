#include "Palettes.hpp"

#include <cstring>

namespace SeaBoy
{
    Palettes::Palettes()
    {
        reset();
    }

    void Palettes::reset()
    {
        m_bgp  = 0xFC; // PanDocs.22 Power Up Sequence
        m_obp0 = 0x00;
        m_obp1 = 0x00;

        m_bcps = 0;
        m_ocps = 0;
        std::memset(m_bcram, 0xFF, sizeof(m_bcram));
        std::memset(m_ocram, 0xFF, sizeof(m_ocram));
    }

    // --- DMG palette resolve ---

    uint32_t Palettes::resolveBG(uint8_t colorID) const
    {
        return m_shades[shadeIndex(m_bgp, colorID)];
    }

    uint32_t Palettes::resolveOBJ(uint8_t colorID, uint8_t paletteNum) const
    {
        uint8_t palette = (paletteNum == 0) ? m_obp0 : m_obp1;
        return m_shades[shadeIndex(palette, colorID)];
    }

    // --- CGB palette data port writes - PanDocs.4.7 CGB Palettes ---

    void Palettes::writeBCPD(uint8_t val)
    {
        m_bcram[m_bcps & 0x3Fu] = val;
        // Auto-increment index if bit 7 is set
        if (m_bcps & 0x80u)
            m_bcps = (m_bcps & 0xC0u) | (((m_bcps & 0x3Fu) + 1u) & 0x3Fu);
    }

    void Palettes::writeOCPD(uint8_t val)
    {
        m_ocram[m_ocps & 0x3Fu] = val;
        if (m_ocps & 0x80u)
            m_ocps = (m_ocps & 0xC0u) | (((m_ocps & 0x3Fu) + 1u) & 0x3Fu);
    }

    // --- CGB 15-bit RGB -> RGBA8888 conversion ---
    // Color format: LE 16-bit, bits 0-4 = R, 5-9 = G, 10-14 = B
    // 5->8 bit expansion: (c << 3) | (c >> 2)

    uint32_t Palettes::cgbColorToRGBA(uint8_t lo, uint8_t hi)
    {
        uint8_t r5 = lo & 0x1Fu;
        uint8_t g5 = static_cast<uint8_t>(((hi & 0x03u) << 3) | (lo >> 5));
        uint8_t b5 = static_cast<uint8_t>((hi >> 2) & 0x1Fu);

        uint8_t r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        uint8_t g = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
        uint8_t b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));

        return (static_cast<uint32_t>(r) << 24) |
               (static_cast<uint32_t>(g) << 16) |
               (static_cast<uint32_t>(b) <<  8) |
               0xFFu;
    }

    uint32_t Palettes::resolveBGCGB(uint8_t paletteIdx, uint8_t colorID) const
    {
        // Each palette = 8 bytes (4 colors × 2 bytes LE)
        uint8_t offset = static_cast<uint8_t>(paletteIdx * 8u + colorID * 2u);
        return cgbColorToRGBA(m_bcram[offset], m_bcram[offset + 1u]);
    }

    uint32_t Palettes::resolveOBJCGB(uint8_t paletteIdx, uint8_t colorID) const
    {
        uint8_t offset = static_cast<uint8_t>(paletteIdx * 8u + colorID * 2u);
        return cgbColorToRGBA(m_ocram[offset], m_ocram[offset + 1u]);
    }
}
