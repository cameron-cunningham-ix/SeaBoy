#include "OAMScan.hpp"

namespace SeaBoy
{
    void OAMScan::scan(const uint8_t* oam, uint8_t ly, uint8_t spriteHeight)
    {
        m_count = 0;

        for (uint8_t i = 0; i < 40 && m_count < 10; ++i)
        {
            uint8_t y    = oam[i * 4 + 0];
            uint8_t x    = oam[i * 4 + 1];
            uint8_t tile = oam[i * 4 + 2];
            uint8_t attr = oam[i * 4 + 3];

            // OAM Y is stored as (sprite_top + 16).
            // Sprite is visible on scanline ly when:  (y-16) <= ly < (y-16+height)
            int top = static_cast<int>(y) - 16;
            if (static_cast<int>(ly) >= top &&
                static_cast<int>(ly) <  top + static_cast<int>(spriteHeight))
            {
                m_sprites[m_count++] = { y, x, tile, attr, i };
            }
        }
    }
}
