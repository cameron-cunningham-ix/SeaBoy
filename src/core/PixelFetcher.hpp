#pragma once

#include <cstdint>

#include "OAMScan.hpp"
#include "Palettes.hpp"

// PanDocs.4.8.1 Pixel FIFO
//
// The pixel fetcher drives Mode 3 (Drawing). Each T-cycle, the BG fetcher state
// machine advances and (when the FIFO is non-empty) one pixel is output.
//
// BG fetcher state machine (5 stages, 2 dots each except Push):
//   GetTile -> GetTileDataLo -> GetTileDataHi -> Sleep -> Push
//   Push retries each dot until BG FIFO is empty, then pushes 8 pixels.
//
// Sprite fetch: triggered when pixelX reaches sprite.x - 8. Pauses BG fetcher
// and pixel output for 6 dots (3 steps × 2 dots), then mixes sprite pixels
// into the OBJ FIFO.
//
// Pixel mixer: pops BG + OBJ FIFO, resolves priority, outputs RGBA to framebuffer.

namespace SeaBoy
{
    // A pixel in the BG/Window FIFO
    struct BgFifoPixel
    {
        uint8_t colorID = 0; // 0-3
    };

    // A pixel in the OBJ FIFO
    struct ObjFifoPixel
    {
        uint8_t colorID    = 0;     // 0-3 (0 = transparent)
        uint8_t palette    = 0;     // 0=OBP0, 1=OBP1
        bool    bgPriority = false; // attr.7: OBJ behind non-zero BG
        bool    occupied   = false; // true = slot has a sprite pixel
    };

    class PixelFetcher
    {
    public:
        // Initialize for a new scanline at Mode 2 -> Mode 3 transition.
        // Snapshots register values and stores pointers to VRAM, sprites, etc.
        void init(const uint8_t* vram,
                  const SpriteEntry* sprites, uint8_t spriteCount,
                  uint8_t lcdc, uint8_t scx, uint8_t scy, uint8_t ly,
                  uint8_t wx, uint8_t windowLineCounter, bool windowTriggered,
                  const Palettes& palettes,
                  uint32_t* frameBufferLine);

        // Advance 1 T-cycle. Returns true when 160 pixels have been output
        // (Mode 3 is complete).
        bool step();

        // Did the window layer render any pixels this scanline?
        bool drewWindow() const { return m_drewWindow; }

    private:
        // --- BG/Window fetcher ---
        // Steps 0,1: GetTile; 2,3: GetTileLo; 4,5: GetTileHi; 6,7: Sleep; 8+: Push
        uint8_t m_bgStep    = 0;
        uint8_t m_bgTileX   = 0;  // current tile column in tilemap (0-31)
        uint8_t m_fetchedTileIndex = 0;
        uint8_t m_fetchedLo = 0;
        uint8_t m_fetchedHi = 0;
        bool    m_inWindow  = false; // currently rendering window (vs BG)

        // BG FIFO - 8-entry ring buffer
        BgFifoPixel m_bgFifo[8]{};
        uint8_t     m_bgFifoHead = 0;
        uint8_t     m_bgFifoSize = 0;

        // OBJ FIFO - 8-entry linear buffer
        ObjFifoPixel m_objFifo[8]{};
        uint8_t      m_objFifoSize = 0;

        // --- Sprite fetch ---
        bool    m_spriteFetch    = false; // sprite fetch in progress
        uint8_t m_objFetchStep   = 0;     // 0-5 (3 steps × 2 dots)
        uint8_t m_objTileIndex   = 0;
        uint8_t m_objSpriteRow   = 0;
        uint8_t m_objFetchedLo   = 0;
        uint8_t m_objFetchedHi   = 0;
        uint8_t m_currentSprite  = 0;     // index into m_sprites[] being fetched
        bool    m_spriteDone[10]{};       // which OAMScan sprites have been fetched

        // --- Pixel output ---
        uint8_t m_pixelX       = 0;   // current output column (0-159)
        uint8_t m_discard      = 0;   // pixels left to discard for SCX fine scroll
        uint8_t m_initialDelay = 0;   // dots to suppress output at Mode 3 start - PanDocs.4.8 Rendering
        bool    m_drewWindow   = false;

        // --- External references (set by init) ---
        const uint8_t*     m_vram        = nullptr;
        const SpriteEntry* m_sprites     = nullptr;
        uint8_t            m_spriteCount = 0;
        const Palettes*    m_palettes    = nullptr;
        uint32_t*          m_fbLine      = nullptr;

        // --- Snapshot of LCD registers ---
        uint8_t m_lcdc             = 0;
        uint8_t m_scx              = 0;
        uint8_t m_scy              = 0;
        uint8_t m_ly               = 0;
        uint8_t m_wx               = 0;
        uint8_t m_windowLineCounter = 0;
        bool    m_windowTriggered  = false;

        // --- Internal helpers ---
        void     bgFetcherTick();
        void     objFetcherTick();
        bool     outputTick();
        void     pushToBgFifo();
        void     mixObjIntoFifo();
        bool     checkSpriteAt() const;
        uint16_t bgTileMapBase() const;
        uint16_t winTileMapBase() const;
        uint16_t tileDataAddr(uint8_t tileIndex) const;
        uint32_t mixPixels(const BgFifoPixel& bg, const ObjFifoPixel& obj) const;

        // BG FIFO helpers
        BgFifoPixel popBgFifo();
        void        clearBgFifo();

        // OBJ FIFO helpers
        ObjFifoPixel popObjFifo();
    };
}
