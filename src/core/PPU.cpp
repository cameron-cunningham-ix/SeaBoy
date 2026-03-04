#include "PPU.hpp"
#include "MMU.hpp"

#include <cstring>

namespace SeaBoy
{
    // Named constants for PPU timing — PanDocs.4.8 Rendering
    constexpr uint32_t DOTS_PER_LINE    = 456;
    constexpr uint8_t  VISIBLE_LINES    = 144;
    constexpr uint8_t  TOTAL_LINES      = 154;
    constexpr uint32_t OAM_SCAN_DOTS    = 80;  // Mode 2 duration
    constexpr uint32_t DRAWING_DOTS_MIN = 172; // Mode 3 minimum duration

    // LCD register addresses — PanDocs.4 LCD I/O Registers
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
        m_mode      = PPUMode::OAMScan;
        m_lineCycle = 0;
        m_ly        = 0;

        m_lcdc = 0x91; // PanDocs.22 Power Up Sequence
        m_stat = 0x00;
        m_scy  = 0x00;
        m_scx  = 0x00;
        m_lyc  = 0x00;
        m_dma  = 0xFF;
        m_wy   = 0x00;
        m_wx   = 0x00;

        m_statLine = false;
        m_palettes.reset();
        std::memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
    }

    void PPU::tick(uint32_t tCycles)
    {
        // LCD disabled: stay in a blanked state — PanDocs.4.4 LCDC.7
        if (!(m_lcdc & LCDC::LCDEnable))
            return;

        for (uint32_t i = 0; i < tCycles; ++i)
        {
            ++m_lineCycle;

            // Mode transitions within a scanline
            if (m_ly < VISIBLE_LINES)
            {
                if (m_lineCycle == OAM_SCAN_DOTS)
                {
                    // Mode 2 -> Mode 3 (Drawing)
                    m_mode = PPUMode::Drawing;
                }
                else if (m_lineCycle == OAM_SCAN_DOTS + DRAWING_DOTS_MIN)
                {
                    // Mode 3 -> Mode 0 (HBlank)
                    // Will compute variable Mode 3 length; fixed 172 for now
                    m_mode = PPUMode::HBlank;
                    updateStatIRQ();
                }
            }

            // End of scanline
            if (m_lineCycle >= DOTS_PER_LINE)
            {
                m_lineCycle = 0;
                ++m_ly;

                if (m_ly >= TOTAL_LINES)
                    m_ly = 0;

                if (m_ly < VISIBLE_LINES)
                {
                    // Start Mode 2 (OAM Scan)
                    m_mode = PPUMode::OAMScan;
                }
                else if (m_ly == VISIBLE_LINES)
                {
                    // Enter VBlank — PanDocs.9.1 INT $40 VBlank interrupt
                    m_mode = PPUMode::VBlank;
                    m_mmu.writeIF(m_mmu.readIF() | 0x01u); // set IF bit 0
                }
                // Lines 145–153 stay in VBlank mode (no transition needed)

                updateStatIRQ();
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

    uint8_t PPU::read(uint16_t addr) const
    {
        switch (addr)
        {
        case ADDR_LCDC: return m_lcdc;
        case ADDR_STAT:
        {
            // PanDocs.9.1 STAT - bits 0-2 are read-only, bit 7 always 1
            uint8_t mode = static_cast<uint8_t>(m_mode);
            uint8_t lycFlag = (m_ly == m_lyc) ? STAT::LYCFlag : 0;
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
                updateStatIRQ();
            }
            break;
        }
        case ADDR_STAT:
            // Only bits 3–6 are writable; bits 0–2 and 7 are read-only
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
            m_dma = val;
            // DMA transfer logic deferred to Phase 3
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