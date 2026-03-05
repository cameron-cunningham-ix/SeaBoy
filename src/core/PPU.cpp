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

    // CGB palette registers - PanDocs.4.7 CGB Palettes
    constexpr uint16_t ADDR_BCPS = 0xFF68;
    constexpr uint16_t ADDR_BCPD = 0xFF69;
    constexpr uint16_t ADDR_OCPS = 0xFF6A;
    constexpr uint16_t ADDR_OCPD = 0xFF6B;

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

        std::memset(m_vram,        0, sizeof(m_vram));
        std::memset(m_oam,         0, sizeof(m_oam));
        std::memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
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
                                  &m_frameBuffer[m_ly * 160]);
                    if (m_fetcher.step())
                    {
                        m_mode = PPUMode::HBlank;
                        if (m_fetcher.drewWindow())
                            ++m_windowLineCounter;
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
        // CGB palette registers - PanDocs.4.7 CGB Palettes
        case ADDR_BCPS: return m_palettes.readBCPS();
        case ADDR_BCPD: return m_palettes.readBCPD();
        case ADDR_OCPS: return m_palettes.readOCPS();
        case ADDR_OCPD: return m_palettes.readOCPD();
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
            // Transfer starts after a 1 M-cycle (4 T-cycle) startup delay
            m_dma           = val;
            m_dmaActive     = true;
            m_dmaStartDelay = 4;
            m_dmaSource     = static_cast<uint16_t>(val << 8);
            m_dmaByte       = 0;
            break;
        case ADDR_BGP:  m_palettes.writeBGP(val); break;
        case ADDR_OBP0: m_palettes.writeOBP0(val); break;
        case ADDR_OBP1: m_palettes.writeOBP1(val); break;
        case ADDR_WY:   m_wy = val; break;
        case ADDR_WX:   m_wx = val; break;
        // CGB palette registers - PanDocs.4.7 CGB Palettes
        case ADDR_BCPS: m_palettes.writeBCPS(val); break;
        case ADDR_BCPD: m_palettes.writeBCPD(val); break;
        case ADDR_OCPS: m_palettes.writeOCPS(val); break;
        case ADDR_OCPD: m_palettes.writeOCPD(val); break;
        default: break;
        }
    }
}
