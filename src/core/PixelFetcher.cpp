#include "PixelFetcher.hpp"

namespace SeaBoy
{
    // LCDC bit masks (duplicated from PPU.hpp to avoid circular include)
    namespace FetcherLCDC
    {
        constexpr uint8_t BGEnable      = 0x01; // bit 0
        constexpr uint8_t OBJEnable     = 0x02; // bit 1
        constexpr uint8_t OBJSize       = 0x04; // bit 2
        constexpr uint8_t BGTileMap     = 0x08; // bit 3
        constexpr uint8_t BGWindowTiles = 0x10; // bit 4
        constexpr uint8_t WindowEnable  = 0x20; // bit 5
        constexpr uint8_t WindowTileMap = 0x40; // bit 6
    }

    void PixelFetcher::init(const uint8_t* vram,
                            const SpriteEntry* sprites, uint8_t spriteCount,
                            uint8_t lcdc, uint8_t scx, uint8_t scy, uint8_t ly,
                            uint8_t wx, uint8_t windowLineCounter,
                            bool windowTriggered,
                            const Palettes& palettes,
                            uint32_t* frameBufferLine,
                            bool cgbMode)
    {
        m_vram        = vram;
        m_cgbMode     = cgbMode;
        m_sprites     = sprites;
        m_spriteCount = spriteCount;
        m_palettes    = &palettes;
        m_fbLine      = frameBufferLine;

        m_lcdc             = lcdc;
        m_scx              = scx;
        m_scy              = scy;
        m_ly               = ly;
        m_wx               = wx;
        m_windowLineCounter = windowLineCounter;
        m_windowTriggered  = windowTriggered;

        // BG fetcher starts at the tile containing the leftmost visible pixel
        m_bgStep       = 0;
        m_bgTileX      = (scx >> 3) & 0x1Fu;
        m_discard      = scx & 7u;
        m_pixelX       = 0;
        m_inWindow     = false;
        m_drewWindow   = false;
        // PanDocs.4.8 Rendering: Mode 3 minimum = 160 + 12 dots; the 12 dots
        // are two initial tile fetches. After the Bug-A fix (step at dot 80),
        // the first push arrives at ~dot 88, but hardware first pixel is at
        // dot 92. Model the remaining 4-dot gap as an output hold-off.
        m_initialDelay = 4;

        m_fetchedTileIndex = 0;
        m_fetchedLo = 0;
        m_fetchedHi = 0;
        m_fetchedAttr = 0;

        // Clear FIFOs
        clearBgFifo();
        m_objFifoSize = 0;
        for (auto& p : m_objFifo) p = ObjFifoPixel{};

        // Sprite fetch state
        m_spriteFetch   = false;
        m_objFetchStep  = 0;
        m_currentSprite = 0;
        for (auto& d : m_spriteDone) d = false;
    }

    bool PixelFetcher::step()
    {
        // Sprite fetch suspends BG fetcher and pixel output
        if (m_spriteFetch)
        {
            objFetcherTick();
            return false;
        }

        // BG fetcher and pixel output run in parallel each dot
        bgFetcherTick();
        return outputTick();
    }

    // -----------------------------------------------------------------------
    // BG fetcher state machine - PanDocs Pixel FIFO
    // -----------------------------------------------------------------------

    void PixelFetcher::bgFetcherTick()
    {
        switch (m_bgStep)
        {
        case 0: // GetTile: first half (idle)
            ++m_bgStep;
            break;

        case 1: // GetTile: second half - read tile index from tilemap
        {
            uint16_t mapBase = m_inWindow ? winTileMapBase() : bgTileMapBase();
            uint8_t tileRow;
            if (m_inWindow)
                tileRow = m_windowLineCounter >> 3;
            else
                tileRow = static_cast<uint8_t>(m_ly + m_scy) >> 3;
            uint16_t mapOffset = static_cast<uint16_t>(
                tileRow * 32u + (m_bgTileX & 0x1Fu));
            m_fetchedTileIndex = m_vram[mapBase + mapOffset];
            // CGB: read tile attribute from VRAM bank 1 at same map offset
            // PanDocs.4.8.1 - BG Map Attributes (CGB)
            m_fetchedAttr = m_cgbMode ? m_vram[0x2000u + mapBase + mapOffset] : 0;
            ++m_bgStep;
            break;
        }

        case 2: // GetTileLo: first half (idle)
            ++m_bgStep;
            break;

        case 3: // GetTileLo: second half - read lo byte of 2bpp tile row
        {
            uint16_t addr = tileDataAddr(m_fetchedTileIndex);
            uint8_t pixelRow;
            if (m_inWindow)
                pixelRow = m_windowLineCounter & 7u;
            else
                pixelRow = static_cast<uint8_t>(m_ly + m_scy) & 7u;
            // CGB: vertical flip (attr bit 6) and VRAM bank (attr bit 3)
            if (m_cgbMode && (m_fetchedAttr & 0x40u))
                pixelRow = 7u - pixelRow;
            uint16_t bankOffset = (m_cgbMode && (m_fetchedAttr & 0x08u)) ? 0x2000u : 0u;
            m_fetchedLo = m_vram[bankOffset + addr + pixelRow * 2u];
            ++m_bgStep;
            break;
        }

        case 4: // GetTileHi: first half (idle)
            ++m_bgStep;
            break;

        case 5: // GetTileHi: second half - read hi byte of 2bpp tile row
        {
            uint16_t addr = tileDataAddr(m_fetchedTileIndex);
            uint8_t pixelRow;
            if (m_inWindow)
                pixelRow = m_windowLineCounter & 7u;
            else
                pixelRow = static_cast<uint8_t>(m_ly + m_scy) & 7u;
            // CGB: vertical flip (attr bit 6) and VRAM bank (attr bit 3)
            if (m_cgbMode && (m_fetchedAttr & 0x40u))
                pixelRow = 7u - pixelRow;
            uint16_t bankOffset = (m_cgbMode && (m_fetchedAttr & 0x08u)) ? 0x2000u : 0u;
            m_fetchedHi = m_vram[bankOffset + addr + pixelRow * 2u + 1u];
            ++m_bgStep;
            break;
        }

        case 6: // Sleep: first half
            ++m_bgStep;
            break;

        case 7: // Sleep: second half
            ++m_bgStep;
            break;

        default: // Push attempt (step >= 8)
            if (m_bgFifoSize == 0)
            {
                pushToBgFifo();
                ++m_bgTileX;
                m_bgTileX &= 0x1Fu; // wrap at 32
                m_bgStep = 0;
            }
            // else stall (don't advance)
            break;
        }
    }

    void PixelFetcher::pushToBgFifo()
    {
        // CGB: horizontal flip (attr bit 5) reverses pixel order
        bool hFlip = m_cgbMode && (m_fetchedAttr & 0x20u);

        // Push 8 pixels decoded from m_fetchedLo/m_fetchedHi
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            // bit 7 = leftmost pixel (normal); bit 0 = leftmost (hFlip)
            uint8_t bitPos = hFlip ? bit : static_cast<uint8_t>(7u - bit);
            uint8_t colorID = static_cast<uint8_t>(
                ((m_fetchedHi >> bitPos) & 1u) << 1u |
                ((m_fetchedLo >> bitPos) & 1u));
            uint8_t idx = (m_bgFifoHead + m_bgFifoSize) & 7u;
            m_bgFifo[idx].colorID     = colorID;
            m_bgFifo[idx].cgbPalette  = m_fetchedAttr & 0x07u;
            m_bgFifo[idx].cgbPriority = (m_fetchedAttr & 0x80u) != 0;
            ++m_bgFifoSize;
        }
    }

    // -----------------------------------------------------------------------
    // Sprite fetcher - PanDocs.4.8.1 Sprite Fetch
    // -----------------------------------------------------------------------

    void PixelFetcher::objFetcherTick()
    {
        const SpriteEntry& sp = m_sprites[m_currentSprite];
        uint8_t spriteHeight = (m_lcdc & FetcherLCDC::OBJSize) ? 16u : 8u;

        switch (m_objFetchStep)
        {
        case 0: // GetObjTile: first half (idle)
            ++m_objFetchStep;
            break;

        case 1: // GetObjTile: second half - compute tile index and row
        {
            int spriteTop = static_cast<int>(sp.y) - 16;
            uint8_t spriteRow = static_cast<uint8_t>(
                static_cast<int>(m_ly) - spriteTop);

            bool yFlip = (sp.attr & 0x40u) != 0;
            if (yFlip)
                spriteRow = static_cast<uint8_t>(spriteHeight - 1u - spriteRow);

            uint8_t tileIndex = sp.tile;
            if (spriteHeight == 16u)
            {
                tileIndex &= 0xFEu;
                if (spriteRow >= 8u)
                {
                    tileIndex |= 0x01u;
                    spriteRow -= 8u;
                }
            }

            m_objTileIndex = tileIndex;
            m_objSpriteRow = spriteRow;
            ++m_objFetchStep;
            break;
        }

        case 2: // GetObjDataLo: first half (idle)
            ++m_objFetchStep;
            break;

        case 3: // GetObjDataLo: second half - read lo byte
        {
            uint16_t rowAddr = static_cast<uint16_t>(
                m_objTileIndex * 16u + m_objSpriteRow * 2u);
            // CGB: OAM attr bit 3 selects VRAM bank for sprite tile data
            uint16_t bankOffset = (m_cgbMode && (sp.attr & 0x08u)) ? 0x2000u : 0u;
            m_objFetchedLo = m_vram[bankOffset + rowAddr];
            ++m_objFetchStep;
            break;
        }

        case 4: // GetObjDataHi: first half (idle)
            ++m_objFetchStep;
            break;

        case 5: // GetObjDataHi: second half - read hi byte, mix into OBJ FIFO
        {
            uint16_t rowAddr = static_cast<uint16_t>(
                m_objTileIndex * 16u + m_objSpriteRow * 2u);
            uint16_t bankOffset = (m_cgbMode && (sp.attr & 0x08u)) ? 0x2000u : 0u;
            m_objFetchedHi = m_vram[bankOffset + rowAddr + 1u];
            mixObjIntoFifo();
            m_spriteDone[m_currentSprite] = true;
            m_spriteFetch = false;
            break;
        }

        default:
            break;
        }
    }

    void PixelFetcher::mixObjIntoFifo()
    {
        const SpriteEntry& sp = m_sprites[m_currentSprite];
        bool xFlip      = (sp.attr & 0x20u) != 0;
        bool bgPriority = (sp.attr & 0x80u) != 0;
        // CGB: OAM attr bits 0-2 = palette index (8 palettes)
        // DMG: OAM attr bit 4 = 0=OBP0, 1=OBP1
        uint8_t palNum  = m_cgbMode ? (sp.attr & 0x07u) : ((sp.attr & 0x10u) ? 1u : 0u);

        int spriteLeft = static_cast<int>(sp.x) - 8;

        // Ensure OBJ FIFO has at least 8 slots
        while (m_objFifoSize < 8)
            m_objFifo[m_objFifoSize++] = ObjFifoPixel{};

        for (uint8_t px = 0; px < 8; ++px)
        {
            int screenX = spriteLeft + static_cast<int>(px);
            if (screenX < static_cast<int>(m_pixelX)) continue; // already passed
            if (screenX >= 160) continue;

            int fifoSlot = screenX - static_cast<int>(m_pixelX);
            if (fifoSlot < 0 || fifoSlot >= 8) continue;

            uint8_t bit = xFlip ? px : static_cast<uint8_t>(7u - px);
            uint8_t colorID = static_cast<uint8_t>(
                ((m_objFetchedHi >> bit) & 1u) << 1u |
                ((m_objFetchedLo >> bit) & 1u));

            // Lower OAM index wins — don't overwrite occupied slots
            if (colorID != 0 && !m_objFifo[fifoSlot].occupied)
            {
                m_objFifo[fifoSlot].colorID    = colorID;
                m_objFifo[fifoSlot].palette    = palNum;
                m_objFifo[fifoSlot].bgPriority = bgPriority;
                m_objFifo[fifoSlot].occupied   = true;
            }
        }
    }

    bool PixelFetcher::checkSpriteAt() const
    {
        if (!(m_lcdc & FetcherLCDC::OBJEnable)) return false;

        for (uint8_t i = 0; i < m_spriteCount; ++i)
        {
            if (m_spriteDone[i]) continue;

            // Sprite screen-left = sprite.x - 8 (can be negative for partially off-screen)
            int spriteLeft = static_cast<int>(m_sprites[i].x) - 8;
            int triggerX = (spriteLeft < 0) ? 0 : spriteLeft;

            if (static_cast<int>(m_pixelX) == triggerX)
                return true;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // Pixel output - runs each T-cycle alongside BG fetcher
    // -----------------------------------------------------------------------

    bool PixelFetcher::outputTick()
    {
        // Hold off pixel output for the first few dots of Mode 3.
        // This models the 2-fetch warm-up overhead (PanDocs.4.8 Rendering: min
        // Mode 3 = 160+12 dots). Value is calibrated against mealybug tests.
        if (m_initialDelay > 0)
        {
            --m_initialDelay;
            return false;
        }

        if (m_bgFifoSize == 0) return false;

        // SCX fine scroll: discard first (SCX & 7) pixels from BG FIFO
        if (m_discard > 0)
        {
            popBgFifo();
            --m_discard;
            return false;
        }

        // Window transition: check if screen pixelX enters window territory
        // PanDocs.4.6  Window
        if (!m_inWindow &&
            (m_lcdc & FetcherLCDC::WindowEnable) &&
            m_windowTriggered)
        {
            int winStartX = static_cast<int>(m_wx) - 7;
            if (winStartX < 0) winStartX = 0;
            if (static_cast<int>(m_pixelX) >= winStartX && m_wx <= 166u)
            {
                // Transition to window: clear BG FIFO, reset fetcher
                clearBgFifo();
                m_inWindow  = true;
                m_bgStep    = 0;
                m_bgTileX   = 0;
                m_drewWindow = true;
                return false; // no pixel output this dot
            }
        }

        // Check for sprite at current pixelX (before outputting)
        if (checkSpriteAt())
        {
            // Find the first unprocessed sprite at this pixelX
            for (uint8_t i = 0; i < m_spriteCount; ++i)
            {
                if (m_spriteDone[i]) continue;
                int spriteLeft = static_cast<int>(m_sprites[i].x) - 8;
                int triggerX = (spriteLeft < 0) ? 0 : spriteLeft;
                if (static_cast<int>(m_pixelX) == triggerX)
                {
                    m_currentSprite = i;
                    m_spriteFetch   = true;
                    m_objFetchStep  = 0;
                    return false; // no pixel output this dot
                }
            }
        }

        // Pop BG pixel
        BgFifoPixel bg = popBgFifo();

        // Pop OBJ pixel (if any)
        ObjFifoPixel obj = popObjFifo();

        // Mix and output
        m_fbLine[m_pixelX] = mixPixels(bg, obj);
        ++m_pixelX;

        return (m_pixelX >= 160);
    }

    uint32_t PixelFetcher::mixPixels(const BgFifoPixel& bg,
                                     const ObjFifoPixel& obj) const
    {
        if (m_cgbMode)
        {
            // PanDocs.4.8.1 CGB pixel mixing
            // LCDC bit 0 = master priority (NOT BG enable like DMG)
            bool masterPriority = (m_lcdc & FetcherLCDC::BGEnable) != 0;

            // OBJ not present or transparent: output BG
            if (!obj.occupied || obj.colorID == 0)
                return m_palettes->resolveBGCGB(bg.cgbPalette, bg.colorID);

            // If master priority is ON, BG can override OBJ when:
            //   - tile priority bit (bg.cgbPriority) is set, OR
            //   - OBJ attr bit 7 (obj.bgPriority) is set
            // AND bg.colorID != 0
            if (masterPriority &&
                (bg.cgbPriority || obj.bgPriority) &&
                bg.colorID != 0)
                return m_palettes->resolveBGCGB(bg.cgbPalette, bg.colorID);

            return m_palettes->resolveOBJCGB(obj.palette, obj.colorID);
        }

        // DMG mode
        bool bgEnable = (m_lcdc & FetcherLCDC::BGEnable) != 0;
        uint8_t effectiveBgColorID = bgEnable ? bg.colorID : 0u;

        // OBJ not present or transparent: output BG
        if (!obj.occupied || obj.colorID == 0)
            return m_palettes->resolveBG(effectiveBgColorID);

        // OBJ behind BG: only applies when BG is enabled and BG colorID != 0
        if (bgEnable && obj.bgPriority && effectiveBgColorID != 0)
            return m_palettes->resolveBG(bg.colorID);

        return m_palettes->resolveOBJ(obj.colorID, obj.palette);
    }

    // -----------------------------------------------------------------------
    // Tile addressing helpers
    // -----------------------------------------------------------------------

    uint16_t PixelFetcher::bgTileMapBase() const
    {
        // LCDC.3: 0 = 0x9800 (VRAM 0x1800), 1 = 0x9C00 (VRAM 0x1C00)
        return (m_lcdc & FetcherLCDC::BGTileMap) ? 0x1C00u : 0x1800u;
    }

    uint16_t PixelFetcher::winTileMapBase() const
    {
        // LCDC.6: 0 = 0x9800 (VRAM 0x1800), 1 = 0x9C00 (VRAM 0x1C00)
        return (m_lcdc & FetcherLCDC::WindowTileMap) ? 0x1C00u : 0x1800u;
    }

    uint16_t PixelFetcher::tileDataAddr(uint8_t tileIndex) const
    {
        // LCDC.4 = 1: 0x8000 method (unsigned, VRAM offset = index * 16)
        // LCDC.4 = 0: 0x8800 method (signed, base at VRAM 0x1000)
        if (m_lcdc & FetcherLCDC::BGWindowTiles)
            return static_cast<uint16_t>(tileIndex * 16u);
        else
            return static_cast<uint16_t>(
                0x1000u + static_cast<int8_t>(tileIndex) * 16);
    }

    // -----------------------------------------------------------------------
    // FIFO helpers
    // -----------------------------------------------------------------------

    BgFifoPixel PixelFetcher::popBgFifo()
    {
        BgFifoPixel p = m_bgFifo[m_bgFifoHead];
        m_bgFifoHead = (m_bgFifoHead + 1u) & 7u;
        --m_bgFifoSize;
        return p;
    }

    void PixelFetcher::clearBgFifo()
    {
        m_bgFifoHead = 0;
        m_bgFifoSize = 0;
    }

    ObjFifoPixel PixelFetcher::popObjFifo()
    {
        if (m_objFifoSize == 0)
            return ObjFifoPixel{};

        ObjFifoPixel p = m_objFifo[0];
        for (uint8_t i = 0; i + 1u < m_objFifoSize; ++i)
            m_objFifo[i] = m_objFifo[i + 1u];
        --m_objFifoSize;
        m_objFifo[m_objFifoSize] = ObjFifoPixel{}; // clear vacated slot
        return p;
    }
}
