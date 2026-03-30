#pragma once

#include <cstdint>

// PanDocs.4.3 OAM Scan - Mode 2
// Scans the 40 OAM entries and collects up to 10 sprites visible on the
// current scanline. Results are passed to the pixel renderer during Mode 3.
//
// OAM entry layout (4 bytes each, 40 entries at 0xFE00-0xFE9F):
//   Byte 0: Y position - sprite top = Y - 16
//   Byte 1: X position - sprite left = X - 8
//   Byte 2: Tile index
//   Byte 3: Attributes/flags

namespace SeaBoy
{
    struct SpriteEntry
    {
        uint8_t y;        // OAM byte 0: Y position (sprite top = y - 16)
        uint8_t x;        // OAM byte 1: X position (sprite left = x - 8)
        uint8_t tile;     // OAM byte 2: tile index
        uint8_t attr;     // OAM byte 3: attributes/flags
        uint8_t oamIndex; // OAM entry index (0-39), for priority resolution
    };

    class OAMScan
    {
    public:
        // Scan OAM and populate the sprite buffer for scanline 'ly'.
        // spriteHeight: 8 for 8×8 mode, 16 for 8×16 mode (LCDC bit 2).
        // Collects at most 10 entries in OAM order (index ascending).
        void scan(const uint8_t* oam, uint8_t ly, uint8_t spriteHeight);

        const SpriteEntry* sprites() const { return m_sprites; }
        uint8_t            count()   const { return m_count; }

    private:
        SpriteEntry m_sprites[10]{};
        uint8_t     m_count = 0;
    };
}
