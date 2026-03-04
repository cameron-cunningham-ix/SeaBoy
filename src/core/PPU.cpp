#include "PPU.hpp"
#include "MMU.hpp"

#include <cstring>

namespace SeaBoy
{
    // Named constants for PPU timing - PanDocs.4.8 Rendering
    constexpr uint32_t DOTS_PER_LINE    = 456;
    constexpr uint8_t  VISIBLE_LINES    = 144;
    constexpr uint8_t  TOTAL_LINES      = 154;
    constexpr uint32_t OAM_SCAN_DOTS    = 80;  // Mode 2 duration
    constexpr uint32_t DRAWING_DOTS_MIN = 172; // Mode 3 minimum duration

    // LCD register addresses - PanDocs.4 LCD I/O Registers
    constexpr uint16_t ADDR_LCDC = 0xFF40;
    constexpr uint16_t ADDR_STAT = 0xFF41;
    constexpr uint16_t ADDR_SCY  = 0xFF42;
    constexpr uint16_t ADDR_SCX  = 0xFF43;
    constexpr uint16_t ADDR_LY   = 0xFF44;
    constexpr uint16_t ADDR_LYC  = 0xFF45;
    constexpr uint16_t ADDR_DMA  = 0xFF46;
    constexpr uint16_t ADDR_BGP  = 0xFF47;
    constexpr uint16_t ADDR_OBP0 = 0xFF48;
    constexpr uint16_t ADDR_OBP1 = 0xFF49;
    constexpr uint16_t ADDR_WY   = 0xFF4A;
    constexpr uint16_t ADDR_WX   = 0xFF4B;

    PPU::PPU(MMU& mmu)
        : m_mmu(mmu)
    {
        reset();
    }

    void PPU::reset()
    {
        m_mode        = PPUMode::OAMScan;
        m_lineCycle   = 0;
        m_ly          = 0;
        m_mode3Length = DRAWING_DOTS_MIN;

        m_lcdc = 0x91; // PanDocs.22 Power Up Sequence
        m_stat = 0x00;
        m_scy  = 0x00;
        m_scx  = 0x00;
        m_lyc  = 0x00;
        m_dma  = 0xFF;
        m_wy   = 0x00;
        m_wx   = 0x00;

        m_statLine          = false;
        m_windowTriggered   = false;
        m_windowLineCounter = 0;
        m_dmaActive = false;
        m_dmaSource = 0;
        m_dmaByte   = 0;
        m_palettes.reset();

        std::memset(m_vram,           0, sizeof(m_vram));
        std::memset(m_oam,            0, sizeof(m_oam));
        std::memset(m_lineBGColorIDs, 0, sizeof(m_lineBGColorIDs));
        std::memset(m_frameBuffer,    0, sizeof(m_frameBuffer));
    }

    void PPU::startOAMScan()
    {
        // PanDocs.4.3 Mode 2 - scan OAM for up to 10 sprites on this line
        uint8_t spriteHeight = (m_lcdc & LCDC::OBJSize) ? 16 : 8;
        m_oamScan.scan(m_oam, m_ly, spriteHeight);

        // Window trigger: armed once when LY >= WY while window is enabled
        // PanDocs.4.6 Window - WY Condition
        if (!m_windowTriggered &&
            (m_lcdc & LCDC::WindowEnable) &&
            m_ly >= m_wy)
        {
            m_windowTriggered = true;
        }

        // Variable Mode 3 length: base 172 + SCX fine-scroll + 6 per sprite
        // +6 if window is active on this line - PanDocs.4.8 Mode 3 Timing
        m_mode3Length = DRAWING_DOTS_MIN + (m_scx & 7u) + m_oamScan.count() * 6u;
        if ((m_lcdc & LCDC::WindowEnable) && m_windowTriggered && m_wx <= 166u)
            m_mode3Length += 6u;
    }

    void PPU::tick(uint32_t tCycles)
    {
        // OAM DMA: 1 byte per M-cycle (4 T-cycles), 160 bytes total = 640 T-cycles
        // DMA runs regardless of LCD state. - PanDocs.4.3.1 OAM DMA Transfer
        if (m_dmaActive)
        {
            uint32_t steps = tCycles / 4;
            while (steps-- > 0 && m_dmaByte < 160)
            {
                m_oam[m_dmaByte] = m_mmu.peek8(
                    static_cast<uint16_t>(m_dmaSource + m_dmaByte));
                ++m_dmaByte;
            }
            if (m_dmaByte >= 160)
                m_dmaActive = false;
        }

        // LCD disabled: stay in a blanked state - PanDocs.4.4 LCDC.7
        if (!(m_lcdc & LCDC::LCDEnable))
            return;

        for (uint32_t i = 0; i < tCycles; ++i)
        {
            ++m_lineCycle;

            // Mode transitions within a visible scanline
            if (m_ly < VISIBLE_LINES)
            {
                if (m_lineCycle == OAM_SCAN_DOTS)
                {
                    // Mode 2 -> Mode 3 (Drawing)
                    m_mode = PPUMode::Drawing;
                }
                else if (m_lineCycle == OAM_SCAN_DOTS + m_mode3Length)
                {
                    // Mode 3 -> Mode 0 (HBlank)
                    m_mode = PPUMode::HBlank;
                    renderBGLine();
                    renderWindowLine();
                    renderSpriteLine();
                    updateStatIRQ();
                }
            }

            // End of scanline
            if (m_lineCycle >= DOTS_PER_LINE)
            {
                m_lineCycle = 0;
                ++m_ly;

                if (m_ly >= TOTAL_LINES)
                {
                    m_ly = 0;
                    // Reset window state for the new frame - PanDocs.4.8.1
                    m_windowTriggered   = false;
                    m_windowLineCounter = 0;
                }

                if (m_ly < VISIBLE_LINES)
                {
                    // Start Mode 2 (OAM Scan) for next visible line
                    m_mode = PPUMode::OAMScan;
                    startOAMScan();
                }
                else if (m_ly == VISIBLE_LINES)
                {
                    // Enter VBlank - PanDocs.9.1 INT $40 VBlank interrupt
                    m_mode = PPUMode::VBlank;
                    m_mmu.writeIF(m_mmu.readIF() | 0x01u); // set IF bit 0
                }
                // Lines 145-153 stay in VBlank mode (no transition needed)

                updateStatIRQ();
            }
        }
    }

    void PPU::renderBGLine()
    {
        // PanDocs.4.3 BG/Window Rendering

        // LCDC bit 0 = 0: BG disabled - fill line with color 0 (white on DMG)
        if (!(m_lcdc & LCDC::BGEnable))
        {
            uint32_t color0 = m_palettes.resolveBG(0);
            for (int x = 0; x < 160; ++x)
            {
                m_lineBGColorIDs[x]         = 0;
                m_frameBuffer[m_ly * 160 + x] = color0;
            }
            return;
        }

        // BG tilemap base in VRAM - PanDocs.4.4 LCDC bit 3
        // LCDC.3 = 0: 0x9800 (VRAM offset 0x1800)
        // LCDC.3 = 1: 0x9C00 (VRAM offset 0x1C00)
        uint16_t tileMapBase = (m_lcdc & LCDC::BGTileMap) ? 0x1C00u : 0x1800u;

        for (int x = 0; x < 160; ++x)
        {
            // Wrapped pixel coordinates in BG map space
            uint8_t mapX = static_cast<uint8_t>(x + m_scx);
            uint8_t mapY = static_cast<uint8_t>(m_ly + m_scy);

            // Tile position in the 32×32 tilemap
            uint16_t tileCol = mapX >> 3;
            uint16_t tileRow = mapY >> 3;

            // Fetch tile index from tilemap
            uint8_t tileIndex = m_vram[tileMapBase + tileRow * 32u + tileCol];

            // Tile data address in VRAM - PanDocs.4.4 LCDC bit 4
            // LCDC.4 = 1: 0x8000 method - unsigned index 0-255, VRAM offset = index*16
            // LCDC.4 = 0: 0x8800 method - signed index, base at VRAM 0x1000
            uint16_t tileAddr;
            if (m_lcdc & LCDC::BGWindowTiles)
                tileAddr = static_cast<uint16_t>(tileIndex * 16u);
            else
                tileAddr = static_cast<uint16_t>(0x1000u + static_cast<int8_t>(tileIndex) * 16);

            // Row within the tile (0-7)
            uint8_t  pixelRow = mapY & 7u;
            uint16_t rowAddr  = static_cast<uint16_t>(tileAddr + pixelRow * 2u);

            // 2 bytes encode 8 pixels: lo byte = bit 0 of color, hi byte = bit 1
            uint8_t lo = m_vram[rowAddr];
            uint8_t hi = m_vram[rowAddr + 1u];

            // Bit position within tile row (7 = leftmost pixel)
            uint8_t bit     = 7u - (mapX & 7u);
            uint8_t colorID = static_cast<uint8_t>(
                ((hi >> bit) & 1u) << 1u | ((lo >> bit) & 1u));

            m_lineBGColorIDs[x]             = colorID;
            m_frameBuffer[m_ly * 160 + x]   = m_palettes.resolveBG(colorID);
        }
    }

    void PPU::renderWindowLine()
    {
        // PanDocs.4.8.1 Window Rendering
        // Window requires: LCDC.5=1, LY >= WY was seen this frame, WX <= 166
        if (!(m_lcdc & LCDC::WindowEnable)) return;
        if (!m_windowTriggered)             return;

        // Effective screen X where window starts (WX=7 -> pixel 0, WX=166 -> pixel 159)
        int winStartX = static_cast<int>(m_wx) - 7;
        if (winStartX >= 160) return; // window entirely off right edge of screen

        // Window tilemap - LCDC bit 6 selects 0x9800 or 0x9C00
        uint16_t tileMapBase = (m_lcdc & LCDC::WindowTileMap) ? 0x1C00u : 0x1800u;

        for (int x = (winStartX < 0 ? 0 : winStartX); x < 160; ++x)
        {
            // Window-local pixel X (0 = left edge of window)
            int winPixelX = x - winStartX;

            // Tile coords within the window's 32×32 tilemap
            uint16_t tileCol = static_cast<uint16_t>(winPixelX >> 3);
            uint16_t tileRow = static_cast<uint16_t>(m_windowLineCounter >> 3);

            uint8_t tileIndex = m_vram[tileMapBase + tileRow * 32u + tileCol];

            // Tile data addressing - same method as BG (LCDC bit 4)
            uint16_t tileAddr;
            if (m_lcdc & LCDC::BGWindowTiles)
                tileAddr = static_cast<uint16_t>(tileIndex * 16u);
            else
                tileAddr = static_cast<uint16_t>(0x1000u + static_cast<int8_t>(tileIndex) * 16);

            uint8_t  pixelRow = static_cast<uint8_t>(m_windowLineCounter & 7u);
            uint16_t rowAddr  = static_cast<uint16_t>(tileAddr + pixelRow * 2u);

            uint8_t lo = m_vram[rowAddr];
            uint8_t hi = m_vram[rowAddr + 1u];

            uint8_t bit     = 7u - static_cast<uint8_t>(winPixelX & 7u);
            uint8_t colorID = static_cast<uint8_t>(
                ((hi >> bit) & 1u) << 1u | ((lo >> bit) & 1u));

            // Overwrite BG color ID and framebuffer for covered pixels
            m_lineBGColorIDs[x]           = colorID;
            m_frameBuffer[m_ly * 160 + x] = m_palettes.resolveBG(colorID);
        }

        // Advance the window's internal line counter
        ++m_windowLineCounter;
    }

    void PPU::renderSpriteLine()
    {
        // PanDocs.4 OAM / Sprites
        if (!(m_lcdc & LCDC::OBJEnable))
            return;

        uint8_t spriteHeight = (m_lcdc & LCDC::OBJSize) ? 16u : 8u;
        const SpriteEntry* sprites = m_oamScan.sprites();
        uint8_t count = m_oamScan.count();

        // Sort sprite indices for back-to-front rendering:
        //   drawn first = lowest priority = highest X (rightmost);
        //   equal X -> highest OAM index.
        // This ensures the last-drawn sprite (lowest X / lowest OAM index) wins.
        // PanDocs.4.3 OAM - DMG sprite priority
        uint8_t order[10];
        for (uint8_t i = 0; i < count; ++i) order[i] = i;

        for (uint8_t i = 1; i < count; ++i)
        {
            uint8_t k = order[i];
            int     j = static_cast<int>(i) - 1;
            while (j >= 0)
            {
                const SpriteEntry& a = sprites[order[j]];
                const SpriteEntry& b = sprites[k];
                // b drawn before a if b has higher X, or equal X and higher OAM index
                bool kBeforeJ = (b.x > a.x) ||
                                (b.x == a.x && b.oamIndex > a.oamIndex);
                if (!kBeforeJ) break;
                order[j + 1] = order[j];
                --j;
            }
            order[j + 1] = k;
        }

        for (uint8_t si = 0; si < count; ++si)
        {
            const SpriteEntry& sp = sprites[order[si]];

            // X=0 or X>=168 means the sprite is fully off-screen horizontally
            if (sp.x == 0 || sp.x >= 168u)
                continue;

            // Row within the sprite tile (Y flip handled below)
            int     spriteTop = static_cast<int>(sp.y) - 16;
            uint8_t spriteRow = static_cast<uint8_t>(static_cast<int>(m_ly) - spriteTop);

            bool yFlip      = (sp.attr & 0x40u) != 0;
            bool xFlip      = (sp.attr & 0x20u) != 0;
            bool bgPriority = (sp.attr & 0x80u) != 0; // OBJ behind BG color 1-3
            uint8_t palNum  = (sp.attr & 0x10u) ? 1u : 0u;

            if (yFlip)
                spriteRow = static_cast<uint8_t>(spriteHeight - 1u - spriteRow);

            // 8×16 mode: tile & 0xFE = top half, tile | 0x01 = bottom half
            uint8_t tileIndex = sp.tile;
            if (spriteHeight == 16u)
            {
                tileIndex &= 0xFEu;
                if (spriteRow >= 8u) { tileIndex |= 0x01u; spriteRow -= 8u; }
            }

            // Sprite tile data always uses 0x8000 base (unsigned index)
            uint16_t rowAddr = static_cast<uint16_t>(tileIndex * 16u + spriteRow * 2u);
            uint8_t  lo      = m_vram[rowAddr];
            uint8_t  hi      = m_vram[rowAddr + 1u];

            for (int px = 0; px < 8; ++px)
            {
                int screenX = static_cast<int>(sp.x) - 8 + px;
                if (screenX < 0 || screenX >= 160) continue;

                uint8_t bit     = xFlip ? static_cast<uint8_t>(px)
                                        : static_cast<uint8_t>(7 - px);
                uint8_t colorID = static_cast<uint8_t>(
                    ((hi >> bit) & 1u) << 1u | ((lo >> bit) & 1u));

                if (colorID == 0) continue; // transparent

                // OBJ-behind-BG: attr.7=1 -> OBJ is behind BG color IDs 1-3
                if (bgPriority && m_lineBGColorIDs[screenX] != 0) continue;

                m_frameBuffer[m_ly * 160 + screenX] =
                    m_palettes.resolveOBJ(colorID, palNum);
            }
        }
    }

    void PPU::updateStatIRQ()
    {
        // PanDocs.9.1 INT 48 STAT interrupt
        // The STAT interrupt line is the OR of all enabled conditions.
        // An interrupt fires only on the rising edge (0->1).
        bool line = false;

        if ((m_stat & STAT::HBlankIRQ)  && m_mode == PPUMode::HBlank)  line = true;
        if ((m_stat & STAT::VBlankIRQ)  && m_mode == PPUMode::VBlank)  line = true;
        if ((m_stat & STAT::OAMScanIRQ) && m_mode == PPUMode::OAMScan) line = true;
        if ((m_stat & STAT::LYCIRQ)     && m_ly == m_lyc)              line = true;

        // Rising edge -> set IF bit 1
        if (line && !m_statLine)
            m_mmu.writeIF(m_mmu.readIF() | 0x02u);

        m_statLine = line;
    }

    uint8_t PPU::readVRAM(uint16_t addr) const
    {
        // VRAM locked during Mode 3 (Drawing) - PanDocs.4.3 LCD Access Timing
        if ((m_lcdc & LCDC::LCDEnable) && m_mode == PPUMode::Drawing)
            return 0xFF;
        return m_vram[addr & 0x1FFFu];
    }

    void PPU::writeVRAM(uint16_t addr, uint8_t val)
    {
        if ((m_lcdc & LCDC::LCDEnable) && m_mode == PPUMode::Drawing)
            return;
        m_vram[addr & 0x1FFFu] = val;
    }

    uint8_t PPU::readOAM(uint16_t addr) const
    {
        // OAM locked during Mode 2 (OAMScan) and Mode 3 (Drawing)
        if ((m_lcdc & LCDC::LCDEnable) &&
            (m_mode == PPUMode::OAMScan || m_mode == PPUMode::Drawing))
            return 0xFF;
        return m_oam[addr - 0xFE00u];
    }

    void PPU::writeOAM(uint16_t addr, uint8_t val)
    {
        if ((m_lcdc & LCDC::LCDEnable) &&
            (m_mode == PPUMode::OAMScan || m_mode == PPUMode::Drawing))
            return;
        m_oam[addr - 0xFE00u] = val;
    }

    uint8_t PPU::read(uint16_t addr) const
    {
        switch (addr)
        {
        case ADDR_LCDC: return m_lcdc;
        case ADDR_STAT:
        {
            // PanDocs.9.1 STAT - bits 0-2 are read-only, bit 7 always 1
            uint8_t mode    = static_cast<uint8_t>(m_mode);
            uint8_t lycFlag = (m_ly == m_lyc) ? STAT::LYCFlag : 0u;
            return (m_stat & 0x78) | 0x80 | lycFlag | mode;
        }
        case ADDR_SCY:  return m_scy;
        case ADDR_SCX:  return m_scx;
        case ADDR_LY:   return m_ly;
        case ADDR_LYC:  return m_lyc;
        case ADDR_DMA:  return m_dma;
        case ADDR_BGP:  return m_palettes.readBGP();
        case ADDR_OBP0: return m_palettes.readOBP0();
        case ADDR_OBP1: return m_palettes.readOBP1();
        case ADDR_WY:   return m_wy;
        case ADDR_WX:   return m_wx;
        default:        return 0xFF;
        }
    }

    void PPU::write(uint16_t addr, uint8_t val)
    {
        switch (addr)
        {
        case ADDR_LCDC:
        {
            bool wasOn = (m_lcdc & LCDC::LCDEnable) != 0;
            bool nowOn = (val & LCDC::LCDEnable) != 0;
            m_lcdc = val;

            // PanDocs.4.4 LCDC.7 - turning LCD off
            if (wasOn && !nowOn)
            {
                m_ly        = 0;
                m_lineCycle = 0;
                m_mode      = PPUMode::HBlank;
                m_statLine  = false;
            }
            // PanDocs.4.4 LCDC.7 - turning LCD on: restart from LY=0 Mode 2
            else if (!wasOn && nowOn)
            {
                m_ly        = 0;
                m_lineCycle = 0;
                m_mode      = PPUMode::OAMScan;
                m_statLine  = false;
                startOAMScan();
                updateStatIRQ();
            }
            break;
        }
        case ADDR_STAT:
            // Only bits 3-6 are writable; bits 0-2 and 7 are read-only
            m_stat = (m_stat & STAT::ModeBits) | (val & 0x78);
            updateStatIRQ(); // new interrupt enables may change the STAT line
            break;
        case ADDR_SCY:  m_scy = val; break;
        case ADDR_SCX:  m_scx = val; break;
        case ADDR_LY:   /* read-only */ break;
        case ADDR_LYC:
            m_lyc = val;
            updateStatIRQ(); // LYC change may trigger/clear LYC match
            break;
        case ADDR_DMA:
            // PanDocs4.3.1 OAM DMA Transfer - initiates a 160-byte copy from
            // (val << 8) into OAM, 1 byte per M-cycle (640 T-cycles total)
            m_dma       = val;
            m_dmaActive = true;
            m_dmaSource = static_cast<uint16_t>(val << 8);
            m_dmaByte   = 0;
            break;
        case ADDR_BGP:  m_palettes.writeBGP(val); break;
        case ADDR_OBP0: m_palettes.writeOBP0(val); break;
        case ADDR_OBP1: m_palettes.writeOBP1(val); break;
        case ADDR_WY:   m_wy = val; break;
        case ADDR_WX:   m_wx = val; break;
        default: break;
        }
    }
}
