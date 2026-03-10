#pragma once

#include <cstdint>

#include "OAMScan.hpp"
#include "Palettes.hpp"
#include "PixelFetcher.hpp"

// PanDocs.4 LCD - Pixel Processing Unit
//
// The PPU renders 160×144 pixels per frame at ~59.7 Hz.
// Each frame = 154 scanlines × 456 T-cycles/scanline = 70224 T-cycles.
//
// Mode state machine per scanline (lines 0–143):
//   Mode 2 (OAM Scan):  80 T-cycles  - scans OAM for sprites on this line
//   Mode 3 (Drawing):   172–289 T-cycles - pixel transfer (variable length)
//   Mode 0 (HBlank):    remainder of 456 T-cycles
// Lines 144–153: Mode 1 (VBlank) for entire scanline.
//
// LCD registers: 0xFF40–0xFF4B
//   LCDC (0xFF40): LCD control
//   STAT (0xFF41): LCD status + interrupt enables
//   SCY  (0xFF42): Scroll Y
//   SCX  (0xFF43): Scroll X
//   LY   (0xFF44): Current scanline (read-only)
//   LYC  (0xFF45): LY compare
//   DMA  (0xFF46): OAM DMA transfer (source address high byte)
//   BGP  (0xFF47): BG palette data (DMG)
//   OBP0 (0xFF48): OBJ palette 0 data (DMG)
//   OBP1 (0xFF49): OBJ palette 1 data (DMG)
//   WY   (0xFF4A): Window Y position
//   WX   (0xFF4B): Window X position + 7

namespace SeaBoy
{
    class MMU; // forward declaration

    // PPU modes - PanDocs STAT
    enum class PPUMode : uint8_t
    {
        HBlank  = 0, // Mode 0
        VBlank  = 1, // Mode 1
        OAMScan = 2, // Mode 2
        Drawing = 3  // Mode 3
    };

    // LCDC bit masks - PanDocs.4.4 LCDC
    namespace LCDC
    {
        constexpr uint8_t BGEnable       = 0x01; // bit 0: BG & Window enable/priority
        constexpr uint8_t OBJEnable      = 0x02; // bit 1: OBJ enable
        constexpr uint8_t OBJSize        = 0x04; // bit 2: OBJ size (0=8x8, 1=8x16)
        constexpr uint8_t BGTileMap      = 0x08; // bit 3: BG tile map area
        constexpr uint8_t BGWindowTiles  = 0x10; // bit 4: BG & Window tile data area
        constexpr uint8_t WindowEnable   = 0x20; // bit 5: Window enable
        constexpr uint8_t WindowTileMap  = 0x40; // bit 6: Window tile map area
        constexpr uint8_t LCDEnable      = 0x80; // bit 7: LCD enable
    }

    // STAT bit masks - PanDocs.4.5 STAT
    namespace STAT
    {
        constexpr uint8_t ModeBits    = 0x03; // bits 0-1: mode (read-only)
        constexpr uint8_t LYCFlag     = 0x04; // bit 2: LYC == LY (read-only)
        constexpr uint8_t HBlankIRQ   = 0x08; // bit 3: Mode 0 interrupt enable
        constexpr uint8_t VBlankIRQ   = 0x10; // bit 4: Mode 1 interrupt enable
        constexpr uint8_t OAMScanIRQ  = 0x20; // bit 5: Mode 2 interrupt enable
        constexpr uint8_t LYCIRQ      = 0x40; // bit 6: LYC == LY interrupt enable
    }

    // OAM corruption trigger type - PanDocs.25 OAM Corruption Bug
    enum class OAMCorruptType { Write, Read, ReadWrite };

    class PPU
    {
    public:
        explicit PPU(MMU& mmu);

        void reset();

        // Advance PPU by tCycles T-cycles. Called from GameBoy::onBusCycle.
        void tick(uint32_t tCycles);

        // LCD register I/O - called by MMU for 0xFF40–0xFF4B
        uint8_t read(uint16_t addr) const;
        void    write(uint16_t addr, uint8_t val);

        // VRAM I/O - called by MMU for 0x8000–0x9FFF
        // Enforces Mode 3 access gating (returns 0xFF / ignores write during Drawing).
        uint8_t readVRAM(uint16_t addr) const;
        void    writeVRAM(uint16_t addr, uint8_t val);

        // OAM I/O - called by MMU for 0xFE00–0xFE9F
        // Enforces Mode 2/3 access gating (returns 0xFF / ignores write during OAMScan/Drawing).
        uint8_t readOAM(uint16_t addr) const;
        void    writeOAM(uint16_t addr, uint8_t val);

        // OAM corruption bug - PanDocs.25 OAM Corruption Bug
        // Called by MMU when a corruption-triggering instruction accesses 0xFE00–0xFEFF
        // while the PPU is in Mode 2 on a visible scanline.
        void triggerOAMCorrupt(OAMCorruptType type);

        // Ungated reads - used by peek8 (debugger) and DMA.
        uint8_t peekVRAM(uint16_t addr) const { return m_vram[addr & 0x1FFFu]; }
        uint8_t peekOAM(uint16_t addr)  const { return m_oam[addr - 0xFE00u]; }

        // Framebuffer access - 160×144 RGBA8888 pixels
        const uint32_t* frameBuffer() const { return m_frameBuffer; }

        // DMA state query - used by MMU to enforce bus conflict - PanDocs OAM DMA
        bool isDMAActive() const { return m_dmaActive; }

        // Palette access
        const Palettes& palettes() const { return m_palettes; }
        Palettes& palettes() { return m_palettes; }

        // Debug-only accessors - read-only snapshots of internal state
        PPUMode  mode()      const { return m_mode; }
        uint8_t  ly()        const { return m_ly; }
        uint32_t lineCycle() const { return m_lineCycle; }
        uint8_t  lcdc()      const { return m_lcdc; }
        uint8_t  stat()      const { return m_stat; }
        uint8_t  scy()       const { return m_scy; }
        uint8_t  scx()       const { return m_scx; }
        uint8_t  lyc()       const { return m_lyc; }
        uint8_t  wy()        const { return m_wy; }
        uint8_t  wx()        const { return m_wx; }
        const uint8_t* rawOAM()  const { return m_oam; }
        const uint8_t* rawVRAM() const { return m_vram; }

    private:
        // STAT interrupt edge detection - PanDocs.9.1 INT 48 STAT interrupt
        void updateStatIRQ();

        // Run OAMScan for the current line.
        // Called whenever we enter Mode 2 (OAMScan).
        void startOAMScan();

        MMU&         m_mmu;
        Palettes     m_palettes;
        OAMScan      m_oamScan;
        PixelFetcher m_fetcher;

        // VRAM (8 KB) and OAM (160 bytes) - owned by PPU - PanDocs.2 Memory Map
        uint8_t m_vram[0x2000]{};
        uint8_t m_oam[160]{};

        // Mode state machine
        PPUMode  m_mode      = PPUMode::OAMScan;
        uint32_t m_lineCycle = 0; // T-cycle within current scanline (0–455)
        uint8_t  m_ly        = 0; // current scanline (0–153)


        // LCD registers
        uint8_t m_lcdc = 0x91; // PanDocs.22 Power Up Sequence: LCD on, BG enabled
        uint8_t m_stat = 0x00; // interrupt enable bits (upper nibble); lower bits computed
        uint8_t m_scy  = 0x00; // scroll Y
        uint8_t m_scx  = 0x00; // scroll X
        uint8_t m_lyc  = 0x00; // LY compare
        uint8_t m_dma  = 0xFF; // DMA register (source high byte)
        uint8_t m_wy   = 0x00; // window Y
        uint8_t m_wx   = 0x00; // window X

        // Window state - PanDocs.4.8.1 Window
        bool    m_windowTriggered   = false; // set when LY >= WY first time this frame
        uint8_t m_windowLineCounter = 0;     // window-internal Y counter (≠ LY)

        // OAM DMA state - PanDocs.4.3.1 OAM DMA Transfer
        bool     m_dmaActive     = false; // DMA in progress
        uint8_t  m_dmaStartDelay = 0;     // T-cycles of startup delay before copy begins
        uint16_t m_dmaSource     = 0;     // source base address (DMA register << 8)
        uint8_t  m_dmaByte       = 0;     // next byte index to copy (0–159)

        // STAT interrupt line - tracks previous combined state for edge detection
        bool    m_statLine  = false;
        // PanDocs LYC: on DMG, the LYC==LY interrupt fires 4 T-cycles after
        // LY updates. -1 = idle; 0-4 = countdown in progress. - Phase 2B
        int8_t  m_lycDelay  = -1;

        // Framebuffer - 160×144 RGBA8888
        uint32_t m_frameBuffer[160 * 144]{};
    };
}
