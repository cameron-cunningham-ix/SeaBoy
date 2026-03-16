#include "PPU.hpp"
#include "MMU.hpp"

#include <algorithm>
#include <cstring>

namespace SeaBoy
{
    // Named constants for PPU timing - PanDocs.4.8 Rendering
    constexpr uint32_t DOTS_PER_LINE    = 456;
    constexpr uint8_t  VISIBLE_LINES    = 144;
    constexpr uint8_t  TOTAL_LINES      = 154;
    constexpr uint32_t OAM_SCAN_DOTS    = 80;  // Mode 2 duration

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
    constexpr uint16_t ADDR_VBK  = 0xFF4F; // CGB VRAM bank select

    // CGB palette registers - PanDocs.4.7 CGB Palettes
    constexpr uint16_t ADDR_BCPS = 0xFF68;
    constexpr uint16_t ADDR_BCPD = 0xFF69;
    constexpr uint16_t ADDR_OCPS = 0xFF6A;
    constexpr uint16_t ADDR_OCPD = 0xFF6B;

    // CGB HDMA registers - PanDocs.10 VRAM DMA Transfers
    constexpr uint16_t ADDR_HDMA1 = 0xFF51;
    constexpr uint16_t ADDR_HDMA2 = 0xFF52;
    constexpr uint16_t ADDR_HDMA3 = 0xFF53;
    constexpr uint16_t ADDR_HDMA4 = 0xFF54;
    constexpr uint16_t ADDR_HDMA5 = 0xFF55;

    PPU::PPU(MMU& mmu)
        : m_mmu(mmu)
    {
        reset();
    }

    void PPU::reset(bool cgb)
    {
        m_cgbMode     = cgb;
        m_vbk         = 0;
        m_mode        = PPUMode::OAMScan;
        m_lineCycle   = 0;
        m_ly          = 0;

        m_lcdc = 0x91; // PanDocs.22 Power Up Sequence
        m_stat = 0x00;
        m_scy  = 0x00;
        m_scx  = 0x00;
        m_lyc  = 0x00;
        m_dma  = 0xFF;
        m_wy   = 0x00;
        m_wx   = 0x00;

        m_statLine          = false;
        m_lycDelay          = -1;
        m_windowTriggered   = false;
        m_windowLineCounter = 0;
        m_dmaActive     = false;
        m_dmaStartDelay = 0;
        m_dmaSource     = 0;
        m_dmaByte       = 0;
        m_palettes.reset();

        m_hdma1 = 0xFF;
        m_hdma2 = 0xFF;
        m_hdma3 = 0xFF;
        m_hdma4 = 0xFF;
        m_hdma5 = 0xFF;
        m_hdmaActive = false;
        m_hdmaSrc    = 0;
        m_hdmaDst    = 0;
        m_hdmaRemain = 0;

        std::memset(m_vram,        0, sizeof(m_vram));
        std::memset(m_oam,         0, sizeof(m_oam));
        std::fill(std::begin(m_frameBuffer), std::end(m_frameBuffer), 0x000000FFu);
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
    }

    void PPU::tick(uint32_t tCycles)
    {
        // OAM DMA: 1 byte per M-cycle (4 T-cycles), 160 bytes total = 640 T-cycles.
        // DMA runs regardless of LCD state. - PanDocs.4.3.1 OAM DMA Transfer
        if (m_dmaActive)
        {
            uint32_t steps = tCycles / 4;
            while (steps-- > 0)
            {
                // PanDocs OAM DMA: 1 M-cycle startup delay before copy begins
                if (m_dmaStartDelay > 0)
                {
                    m_dmaStartDelay -= 4; // consume 1 M-cycle of delay
                    continue;
                }
                if (m_dmaByte >= 160) break;
                m_oam[m_dmaByte] = m_mmu.peek8(
                    static_cast<uint16_t>(m_dmaSource + m_dmaByte));
                ++m_dmaByte;
            }
            if (m_dmaStartDelay == 0 && m_dmaByte >= 160)
                m_dmaActive = false;
        }

        // LCD disabled: stay in a blanked state - PanDocs.4.4 LCDC.7
        if (!(m_lcdc & LCDC::LCDEnable))
            return;

        for (uint32_t i = 0; i < tCycles; ++i)
        {
            // LYC coincidence interrupt fires 4 T-cycles after LY updates.
            // Count down each dot; fire when it reaches 0. - PanDocs LYC, Phase 2B
            if (m_lycDelay > 0)
            {
                if (--m_lycDelay == 0)
                {
                    m_lycDelay = -1;
                    updateStatIRQ();
                }
            }

            ++m_lineCycle;

            // Mode transitions within a visible scanline
            if (m_ly < VISIBLE_LINES)
            {
                if (m_lineCycle == OAM_SCAN_DOTS)
                {
                    // Mode 2 -> Mode 3 (Drawing): initialize pixel fetcher.
                    // PanDocs.4.8 Rendering: fetcher takes its first step on the
                    // same dot Mode 3 begins (dot 80), not dot 81.
                    m_mode = PPUMode::Drawing;
                    // PanDocs.9.1 STAT: notify edge-detector so the OAMScan
                    // interrupt source clears before Mode 3 begins. - Phase 2A
                    updateStatIRQ();
                    m_fetcher.init(m_vram,
                                  m_oamScan.sprites(), m_oamScan.count(),
                                  m_lcdc, m_scx, m_scy, m_ly, m_wx,
                                  m_windowLineCounter, m_windowTriggered,
                                  m_palettes,
                                  &m_frameBuffer[m_ly * 160],
                                  m_cgbMode);
                    if (m_fetcher.step())
                    {
                        m_mode = PPUMode::HBlank;
                        if (m_fetcher.drewWindow())
                            ++m_windowLineCounter;
                        // PanDocs.10 HDMA HBlank transfer: 16 bytes on Mode 3->0
                        if (m_hdmaActive)
                            hdmaTransfer16();
                        updateStatIRQ();
                    }
                }
                else if (m_mode == PPUMode::Drawing)
                {
                    // Advance pixel fetcher 1 T-cycle; Mode 3 ends when
                    // 160 pixels have been output.
                    if (m_fetcher.step())
                    {
                        m_mode = PPUMode::HBlank;
                        if (m_fetcher.drewWindow())
                            ++m_windowLineCounter;
                        // PanDocs.10 HDMA HBlank transfer: 16 bytes on Mode 3->0
                        if (m_hdmaActive)
                            hdmaTransfer16();
                        updateStatIRQ();
                    }
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

                // PanDocs LYC: arm 4-dot delay for LYC==LY interrupt - Phase 2B
                if (m_lyc == m_ly)
                    m_lycDelay = 4;

                updateStatIRQ(); // mode changes fire immediately; LYC is suppressed
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
        // LYC fires only when no delay is pending (m_lycDelay == -1) - Phase 2B
        if (m_lycDelay < 0 && (m_stat & STAT::LYCIRQ) && m_ly == m_lyc) line = true;

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
        // PanDocs.10 VBK — CGB VRAM bank offset
        uint16_t offset = (addr & 0x1FFFu) + (m_cgbMode ? (static_cast<uint16_t>(m_vbk & 1) << 13) : 0u);
        return m_vram[offset];
    }

    void PPU::writeVRAM(uint16_t addr, uint8_t val)
    {
        if ((m_lcdc & LCDC::LCDEnable) && m_mode == PPUMode::Drawing)
            return;
        uint16_t offset = (addr & 0x1FFFu) + (m_cgbMode ? (static_cast<uint16_t>(m_vbk & 1) << 13) : 0u);
        m_vram[offset] = val;
    }

    uint8_t PPU::readOAM(uint16_t addr) const
    {
        // PanDocs.4.3.1 OAM DMA Transfer — when DMA is active, the DMA controller
        // owns the OAM bus, overriding normal PPU mode-based locking.
        if (m_dmaActive)
        {
            // Fresh DMA startup: OAM accessible for 1 M-cycle (mooneye oam_dma_start M=1)
            // Restart DMA startup: old DMA held the bus, OAM stays locked throughout
            if (m_dmaStartDelay > 0 && !m_dmaWasRestart)
                return m_oam[addr - 0xFE00u];
            return 0xFF;
        }
        // OAM locked during Mode 2 (OAMScan) and Mode 3 (Drawing)
        if ((m_lcdc & LCDC::LCDEnable) &&
            (m_mode == PPUMode::OAMScan || m_mode == PPUMode::Drawing))
            return 0xFF;
        return m_oam[addr - 0xFE00u];
    }

    void PPU::writeOAM(uint16_t addr, uint8_t val)
    {
        // PanDocs.4.3.1 OAM DMA Transfer — DMA owns the OAM bus
        if (m_dmaActive)
        {
            // Fresh startup: OAM still writable; restart startup: locked (old DMA held bus)
            if (m_dmaStartDelay > 0 && !m_dmaWasRestart)
                m_oam[addr - 0xFE00u] = val;
            return;
        }
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
        // CGB VRAM bank select - PanDocs.10 VBK
        case ADDR_VBK:  return m_vbk | 0xFEu;
        // CGB palette registers - PanDocs.4.7 CGB Palettes
        case ADDR_BCPS: return m_palettes.readBCPS();
        case ADDR_BCPD: return m_palettes.readBCPD();
        case ADDR_OCPS: return m_palettes.readOCPS();
        case ADDR_OCPD: return m_palettes.readOCPD();
        // CGB HDMA registers - PanDocs.10 VRAM DMA Transfers
        case ADDR_HDMA1: return m_hdma1;
        case ADDR_HDMA2: return m_hdma2;
        case ADDR_HDMA3: return m_hdma3;
        case ADDR_HDMA4: return m_hdma4;
        case ADDR_HDMA5:
            // Bit 7: 0 = HDMA active, 1 = HDMA not active (inverted!)
            // Bits 0-6: remaining blocks - 1 (or 0x7F when inactive)
            if (!m_hdmaActive)
                return 0xFFu;
            return static_cast<uint8_t>(((m_hdmaRemain / 16) - 1) & 0x7Fu);
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
            // PanDocs.4.3.1 OAM DMA Transfer - initiates a 160-byte copy from
            // (val << 8) into OAM, 1 byte per M-cycle (640 T-cycles total).
            //
            // Fresh start: 2 M-cycle startup delay (8 T-cycles). The triggering
            // write's own bus tick consumes 4T, leaving 4T = 1 M-cycle where OAM
            // is still accessible (mooneye oam_dma_start: M=1 OAM readable, M=2 locked).
            //
            // Restart while DMA already active: the old DMA keeps OAM locked through
            // M=1 of the new write, so the new DMA must start at M=2 with only
            // 1 M-cycle of startup delay (4 T-cycles). This way the write's own bus tick
            // consumes the full delay, and readOAM at M=1 sees dmaStartDelay=0 -> locked.
            // Two behaviours verified by mooneye oam_dma_start + oam_dma_restart:
            //   Fresh start:   2 M-cycle startup (8T). The write's own bus tick consumes 4T,
            //                  leaving 1 M-cycle where OAM is still accessible (oam_dma_start M=1).
            //   Restart:       Same 2 M-cycle startup (8T), but OAM must stay locked during it
            //                  because the old DMA was still holding the bus. m_dmaWasRestart
            //                  signals readOAM/writeOAM to return $FF / ignore writes even while
            //                  dmaStartDelay > 0 (oam_dma_start round2: B=0, oam_dma_restart timing).
            m_dma            = val;
            m_dmaWasRestart  = m_dmaActive;
            m_dmaStartDelay  = 8;
            m_dmaActive      = true;
            m_dmaSource      = static_cast<uint16_t>(val << 8);
            m_dmaByte        = 0;
            break;
        case ADDR_BGP:  m_palettes.writeBGP(val); break;
        case ADDR_OBP0: m_palettes.writeOBP0(val); break;
        case ADDR_OBP1: m_palettes.writeOBP1(val); break;
        case ADDR_WY:   m_wy = val; break;
        case ADDR_WX:   m_wx = val; break;
        // CGB VRAM bank select - PanDocs.10 VBK
        case ADDR_VBK:  if (m_cgbMode) m_vbk = val & 0x01u; break;
        // CGB palette registers - PanDocs.4.7 CGB Palettes
        case ADDR_BCPS: m_palettes.writeBCPS(val); break;
        case ADDR_BCPD: m_palettes.writeBCPD(val); break;
        case ADDR_OCPS: m_palettes.writeOCPS(val); break;
        case ADDR_OCPD: m_palettes.writeOCPD(val); break;
        // CGB HDMA registers - PanDocs.10 VRAM DMA Transfers
        case ADDR_HDMA1: m_hdma1 = val; break;
        case ADDR_HDMA2: m_hdma2 = val & 0xF0u; break; // lower 4 bits ignored
        case ADDR_HDMA3: m_hdma3 = val & 0x1Fu; break;  // only bits 0-4 (dest is 0x8000-0x9FF0)
        case ADDR_HDMA4: m_hdma4 = val & 0xF0u; break; // lower 4 bits ignored
        case ADDR_HDMA5: writeHDMA5(val); break;
        default: break;
        }
    }

    // PanDocs.10 VRAM DMA Transfers — HDMA5 write handler
    void PPU::writeHDMA5(uint8_t val)
    {
        if (!m_cgbMode) return;

        // Writing with bit 7 = 0 while HBlank DMA is active: cancel it
        if (m_hdmaActive && !(val & 0x80u))
        {
            m_hdmaActive = false;
            m_hdma5 = val | 0x80u; // bit 7 set = inactive
            return;
        }

        // Compute source and destination from HDMA1-4
        m_hdmaSrc = static_cast<uint16_t>((m_hdma1 << 8) | m_hdma2);
        m_hdmaDst = static_cast<uint16_t>(0x8000u | ((m_hdma3 << 8) | m_hdma4));
        m_hdmaRemain = static_cast<uint16_t>(((val & 0x7Fu) + 1) * 16);

        if (val & 0x80u)
        {
            // HBlank DMA: transfers 16 bytes per Mode 3->0 transition
            m_hdmaActive = true;
        }
        else
        {
            // General DMA: immediate block copy
            // CPU is halted for the duration. We do the entire copy now.
            while (m_hdmaRemain > 0)
                hdmaTransfer16();
            m_hdmaActive = false;
        }
    }

    // Transfer 16 bytes from source to VRAM destination.
    // Source is read via m_mmu.peek8(); dest writes directly to m_vram with bank offset.
    void PPU::hdmaTransfer16()
    {
        for (int i = 0; i < 16; ++i)
        {
            uint8_t byte = m_mmu.peek8(m_hdmaSrc);
            // Write to VRAM at current bank (destination is always in 0x8000-0x9FFF)
            uint16_t vramOffset = (m_hdmaDst & 0x1FFFu) +
                (m_cgbMode ? (static_cast<uint16_t>(m_vbk & 1) << 13) : 0u);
            m_vram[vramOffset] = byte;
            ++m_hdmaSrc;
            ++m_hdmaDst;
        }
        m_hdmaRemain -= 16;

        if (m_hdmaRemain == 0)
            m_hdmaActive = false;
    }

    void PPU::triggerOAMCorrupt(OAMCorruptType type)
    {
        // Gate: LCD on, visible line, PPU in Mode 2 - PanDocs.25 OAM Corruption Bug
        if (!(m_lcdc & LCDC::LCDEnable)) return;
        if (m_ly >= VISIBLE_LINES)       return;
        if (m_mode != PPUMode::OAMScan)  return;

        // One OAM row (8 bytes = 4 × 16-bit words) is read per M-cycle during Mode 2.
        // row 0 (objects 0-1) is protected from corruption.
        uint8_t row = static_cast<uint8_t>(m_lineCycle / 4);
        if (row == 0) return;

        uint8_t* cur  = m_oam + row * 8;
        uint8_t* prev = m_oam + (row - 1) * 8;

        // 16-bit word helpers (little-endian)
        auto rw = [](const uint8_t* p, int w) -> uint16_t
            { return static_cast<uint16_t>(p[w * 2] | (p[w * 2 + 1] << 8)); };
        auto ww = [](uint8_t* p, int w, uint16_t v)
            { p[w * 2] = static_cast<uint8_t>(v); p[w * 2 + 1] = static_cast<uint8_t>(v >> 8); };

        if (type == OAMCorruptType::Write)
        {
            // PanDocs.25 OAM Corruption Bug - Write Corruption
            uint16_t a = rw(cur, 0), b = rw(prev, 0), c = rw(prev, 2);
            ww(cur, 0, static_cast<uint16_t>(((a ^ c) & (b ^ c)) ^ c));
            for (int i = 1; i < 4; ++i) ww(cur, i, rw(prev, i));
        }
        else if (type == OAMCorruptType::Read)
        {
            // PanDocs.25 OAM Corruption Bug - Read Corruption
            uint16_t a = rw(cur, 0), b = rw(prev, 0), c = rw(prev, 2);
            ww(cur, 0, static_cast<uint16_t>(b | (a & c)));
            for (int i = 1; i < 4; ++i) ww(cur, i, rw(prev, i));
        }
        else // ReadWrite
        {
            // PanDocs.25 OAM Corruption Bug - Read During Increase/Decrease
            // Multi-row step only applies when not in the first four or last row.
            if (row >= 4 && row < 19)
            {
                uint8_t* prev2 = m_oam + (row - 2) * 8;
                uint16_t a = rw(prev2, 0), b = rw(prev, 0), c = rw(cur, 0), d = rw(prev, 2);
                ww(prev, 0, static_cast<uint16_t>((b & (a | c | d)) | (a & c & d)));
                for (int i = 0; i < 4; ++i)
                {
                    ww(cur,   i, rw(prev, i));
                    ww(prev2, i, rw(prev, i));
                }
            }
            // Follow with Read corruption applied to (updated) cur
            uint16_t a = rw(cur, 0), b = rw(prev, 0), c = rw(prev, 2);
            ww(cur, 0, static_cast<uint16_t>(b | (a & c)));
            for (int i = 1; i < 4; ++i) ww(cur, i, rw(prev, i));
        }
    }
}
