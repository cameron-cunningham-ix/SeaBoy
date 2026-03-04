#include "Palettes.hpp"

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
    }

    uint32_t Palettes::resolveBG(uint8_t colorID) const
    {
        return s_shades[shadeIndex(m_bgp, colorID)];
    }

    uint32_t Palettes::resolveOBJ(uint8_t colorID, uint8_t paletteNum) const
    {
        uint8_t palette = (paletteNum == 0) ? m_obp0 : m_obp1;
        return s_shades[shadeIndex(palette, colorID)];
    }
}