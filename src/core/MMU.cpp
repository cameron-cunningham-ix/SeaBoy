#include "MMU.hpp"

#include "../cartridge/Cartridge.hpp"
#include "APU.hpp"
#include "Joypad.hpp"
#include "PPU.hpp"
#include "Timer.hpp"

#include <algorithm>
#include <cstring>

namespace SeaBoy
{
    MMU::MMU()
    {
        reset();
    }

    // Defined here so unique_ptr<Cartridge> is destructed where Cartridge is fully visible.
    MMU::~MMU() = default;

    void MMU::reset()
    {
        // Cartridge is not reset here - it is replaced wholesale by loadROM().
        // VRAM and OAM are owned by PPU; cleared by PPU::reset().
        std::memset(m_wram, 0x00, sizeof(m_wram));
        std::memset(m_hram, 0x00, sizeof(m_hram));
        m_ifReg = 0xE1; // PanDocs Power Up Sequence
        m_ie    = 0x00;
        m_svbk    = 0;
        m_cgbMode = false;
        m_key1    = 0;
        m_opri = 0x00;
        m_rp = 0x00;

        m_sb = 0;
        m_sc = 0;
        m_serialOutput.clear();
    }

    void MMU::loadROM(const uint8_t* data, size_t size)
    {
        std::vector<uint8_t> romData(data, data + size);
        m_cart = Cartridge::create(std::move(romData));
    }

    void MMU::enableTestMode()
    {
        m_testRam = std::make_unique<uint8_t[]>(65536);
        std::memset(m_testRam.get(), 0x00, 65536);
        m_ifReg = 0; // no pending interrupts; IF must not alias test RAM at 0xFF0F
    }

    bool MMU::isDMAActive() const
    {
        return m_ppu && m_ppu->isDMAActive();
    }

    // PanDocs.2 SVBK — CGB WRAM bank offset for 0xD000–0xDFFF region.
    // Bank 0 maps to 1. Returns byte offset into m_wram[].
    static inline uint32_t wramBankOffset(uint8_t svbk)
    {
        uint8_t bank = svbk & 0x07u;
        if (bank == 0) bank = 1;
        return static_cast<uint32_t>(bank) * 0x1000u;
    }

    // PanDocs.2 - Memory Map routing
    // Each read8/write8 = 1 M-cycle = 4 T-cycles on the bus.
    uint8_t MMU::read8(uint16_t addr)
    {
        if (m_testRam)
            return m_testRam[addr];

        // PanDocs OAM DMA: during an active DMA transfer the CPU loses access
        // to ROM/WRAM/ERAM/VRAM (the bus the DMA controller is using).
        // I/O registers (0xFF00–0xFF7F) and OAM/prohibited (0xFE00–0xFEFF)
        // are on separate buses and remain accessible.
        if (isDMAActive() && addr < 0xFE00u)
        {
            if (m_cycleFn) m_cycleFn(m_cycleCtx, 4);
            return 0xFFu;
        }

        uint8_t val = 0xFFu; // Open bus default

        // ROM (bank 0 + switchable bank) and external RAM - delegated to Cartridge
        if (addr <= ADDR_ROM_END)
            val = m_cart ? m_cart->read(addr) : 0xFFu;
        // 0x8000–0x9FFF: VRAM - routed to PPU
        else if (addr >= 0x8000u && addr <= 0x9FFFu)
            val = m_ppu ? m_ppu->readVRAM(addr) : 0xFFu;
        else if (addr >= ADDR_ERAM_BASE && addr <= ADDR_ERAM_END)
            val = m_cart ? m_cart->read(addr) : 0xFFu;
        // WRAM: 0xC000–0xCFFF = bank 0, 0xD000–0xDFFF = switchable (SVBK) - PanDocs.2
        else if (addr >= 0xC000u && addr <= 0xCFFFu)
            val = m_wram[addr - 0xC000u];
        else if (addr >= 0xD000u && addr <= 0xDFFFu)
            val = m_wram[wramBankOffset(m_svbk) + (addr - 0xD000u)];
        // Echo RAM: 0xE000–0xEFFF mirrors bank 0, 0xF000–0xFDFF mirrors switchable - PanDocs.2
        else if (addr >= 0xE000u && addr <= 0xEFFFu)
            val = m_wram[addr - 0xE000u];
        else if (addr >= 0xF000u && addr <= 0xFDFFu)
            val = m_wram[wramBankOffset(m_svbk) + (addr - 0xF000u)];
        // 0xFE00–0xFE9F: OAM - routed to PPU
        else if (addr >= 0xFE00u && addr <= 0xFE9Fu)
            val = m_ppu ? m_ppu->readOAM(addr) : 0xFFu;
        // Prohibited area: 0xFEA0–0xFEFF returns 0x00 on DMG - PanDocs.2
        else if (addr >= 0xFEA0u && addr <= 0xFEFFu)
            val = 0x00u;
        // I/O registers (0xFF00–0xFF7F)
        // Joypad P1 register - PanDocs.6 Joypad Input
        else if (addr == ADDR_P1)
            val = m_joypad ? m_joypad->read() : 0xFFu;
        else if (addr == ADDR_IF)
            val = m_ifReg | 0xE0u; // upper 3 bits always 1
        else if (addr == 0xFF01u)
            val = m_sb;
        else if (addr == 0xFF02u)
            val = m_sc | 0x7Eu;    // unused bits read as 1
        // Timer registers - PanDocs.8 Timer and Divider Registers
        else if (addr >= ADDR_DIV && addr <= ADDR_TAC)
            val = m_timer ? m_timer->read(addr) : 0xFFu;
        // APU registers - PanDocs Audio Registers
        else if ((addr >= 0xFF10u && addr <= 0xFF26u) || (addr >= 0xFF30u && addr <= 0xFF3Fu))
            val = m_apu ? m_apu->read(addr) : 0xFFu;
        // LCD registers - routed to PPU (PanDocs.4 LCD I/O Registers)
        else if (addr >= 0xFF40u && addr <= 0xFF4Bu)
            val = m_ppu ? m_ppu->read(addr) : 0xFFu;
        // CGB speed switching KEY1 (0xFF4D) - PanDocs.10
        else if (addr == 0xFF4Du)
            val = m_cgbMode ? (m_key1 | 0x7Eu) : 0xFFu; // only bits 0 and 7 meaningful
        // CGB VRAM bank select (0xFF4F) - routed to PPU
        else if (addr == 0xFF4Fu)
            val = (m_cgbMode && m_ppu) ? m_ppu->read(addr) : 0xFFu;
        // CGB HDMA registers (0xFF51-0xFF55) - routed to PPU
        else if (addr >= 0xFF51u && addr <= 0xFF55u)
            val = (m_cgbMode && m_ppu) ? m_ppu->read(addr) : 0xFFu;
        // CGB IR - bits 2-5 always 1; bit 7 (write enable) and bit 0 (LED) are R/W; bit 1 = recieved signal = 0
        else if (addr == 0xFF56u)
            val = m_cgbMode ? ((m_rp & 0xC1u) | 0x3Eu) : 0xFFu;
        // CGB palette registers - routed to PPU (PanDocs.4.7)
        else if (addr >= 0xFF68u && addr <= 0xFF6Bu)
            val = (m_cgbMode && m_ppu) ? m_ppu->read(addr) : 0xFFu;
        // CGB object priority mode
        else if (addr == 0xFF6Cu)
            val = m_cgbMode ? ((m_opri & 0x01u) | 0xFEu) : 0xFFu;
        // CGB WRAM bank select (0xFF70) - PanDocs.2 SVBK
        else if (addr == 0xFF70u)
            val = m_cgbMode ? (m_svbk | 0xF8u) : 0xFFu;
        else if (addr >= ADDR_HRAM_BASE && addr <= ADDR_HRAM_END)
            val = m_hram[addr - ADDR_HRAM_BASE];
        else if (addr == ADDR_IE)
            val = m_ie;

        // Tick subsystems after each bus access (4 T-cycles per M-cycle)
        if (m_cycleFn) m_cycleFn(m_cycleCtx, 4);
        return val;
    }

    void MMU::write8(uint16_t addr, uint8_t val)
    {
        if (m_testRam)
        {
            m_testRam[addr] = val;
            return;
        }

        // ROM area: MBC register writes (bank switching etc.) - delegated to Cartridge
        if (addr <= ADDR_ROM_END)
            { if (m_cart) m_cart->write(addr, val); }
        // 0x8000–0x9FFF: VRAM - routed to PPU
        else if (addr >= 0x8000u && addr <= 0x9FFFu)
            { if (m_ppu) m_ppu->writeVRAM(addr, val); }
        // 0xA000–0xBFFF: external RAM writes - delegated to Cartridge
        else if (addr >= ADDR_ERAM_BASE && addr <= ADDR_ERAM_END)
            { if (m_cart) m_cart->write(addr, val); }
        // WRAM: 0xC000–0xCFFF = bank 0, 0xD000–0xDFFF = switchable (SVBK) - PanDocs.2
        else if (addr >= 0xC000u && addr <= 0xCFFFu)
            m_wram[addr - 0xC000u] = val;
        else if (addr >= 0xD000u && addr <= 0xDFFFu)
            m_wram[wramBankOffset(m_svbk) + (addr - 0xD000u)] = val;
        // Echo RAM: 0xE000–0xEFFF mirrors bank 0, 0xF000–0xFDFF mirrors switchable - PanDocs.2
        else if (addr >= 0xE000u && addr <= 0xEFFFu)
            m_wram[addr - 0xE000u] = val;
        else if (addr >= 0xF000u && addr <= 0xFDFFu)
            m_wram[wramBankOffset(m_svbk) + (addr - 0xF000u)] = val;
        // 0xFE00–0xFE9F: OAM - routed to PPU
        else if (addr >= 0xFE00u && addr <= 0xFE9Fu)
            { if (m_ppu) m_ppu->writeOAM(addr, val); }
        // Prohibited area: 0xFEA0–0xFEFF - writes ignored on DMG - PanDocs.2
        else if (addr >= 0xFEA0u && addr <= 0xFEFFu)
            { /* ignored */ }
        // Joypad P1 register - PanDocs.6 Joypad Input
        else if (addr == ADDR_P1)
            { if (m_joypad) m_joypad->write(val); }
        else if (addr == ADDR_IF)
            m_ifReg = val & 0x1Fu; // only lower 5 bits are writable
        // Timer registers - PanDocs.8 Timer and Divider Registers
        else if (addr >= ADDR_DIV && addr <= ADDR_TAC)
            { if (m_timer) m_timer->write(addr, val); }
        // APU registers - PanDocs Audio Registers
        else if ((addr >= 0xFF10u && addr <= 0xFF26u) || (addr >= 0xFF30u && addr <= 0xFF3Fu))
            { if (m_apu) m_apu->write(addr, val); }
        // LCD registers - routed to PPU (PanDocs.4 LCD I/O Registers)
        else if (addr >= 0xFF40u && addr <= 0xFF4Bu)
            { if (m_ppu) m_ppu->write(addr, val); }
        // CGB speed switching KEY1 (0xFF4D) - PanDocs.10
        else if (addr == 0xFF4Du)
            { if (m_cgbMode) m_key1 = (m_key1 & 0x80u) | (val & 0x01u); } // only bit 0 writable
        // CGB VRAM bank select (0xFF4F) - routed to PPU
        else if (addr == 0xFF4Fu)
            { if (m_ppu) m_ppu->write(addr, val); }
        // CGB HDMA registers (0xFF51-0xFF55) - routed to PPU
        else if (addr >= 0xFF51u && addr <= 0xFF55u)
            { if (m_ppu) m_ppu->write(addr, val); }
        // CGB IR
        else if (addr == 0xFF56u)
            { if (m_cgbMode) m_rp = val & 0xC1u; }  // only bits 7 and 0 writable
        // CGB palette registers - routed to PPU (PanDocs.4.7)
        else if (addr >= 0xFF68u && addr <= 0xFF6Bu)
            { if (m_ppu) m_ppu->write(addr, val); }
        // CGB object priority mode
        else if (addr == 0xFF6Cu)
            { if (m_cgbMode) m_opri = val & 0x01u; }
        // CGB WRAM bank select (0xFF70) - PanDocs.2 SVBK
        else if (addr == 0xFF70u)
            { if (m_cgbMode) m_svbk = val & 0x07u; }
        // Serial port – PanDocs.7 Serial Data Transfer
        else if (addr == 0xFF01u)
            m_sb = val;
        else if (addr == 0xFF02u)
        {
            m_sc = val;
            if (val & 0x80u) // Transfer start (internal clock)
            {
                m_serialOutput += static_cast<char>(m_sb);
                m_sc &= ~0x80u; // Clear start bit – transfer completes "instantly"
            }
        }
        else if (addr >= ADDR_HRAM_BASE && addr <= ADDR_HRAM_END)
            m_hram[addr - ADDR_HRAM_BASE] = val;
        else if (addr == ADDR_IE)
            m_ie = val;
        // else: writes to unmapped regions are silently ignored

        // Tick subsystems after each bus access (4 T-cycles per M-cycle)
        if (m_cycleFn) m_cycleFn(m_cycleCtx, 4);
    }

    uint16_t MMU::read16(uint16_t addr)
    {
        // Little-endian: low byte at addr, high byte at addr+1
        uint8_t lo = read8(addr);
        uint8_t hi = read8(static_cast<uint16_t>(addr + 1));
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    void MMU::write16(uint16_t addr, uint16_t val)
    {
        // Little-endian: low byte first
        write8(addr, static_cast<uint8_t>(val & 0xFF));
        write8(static_cast<uint16_t>(addr + 1), static_cast<uint8_t>(val >> 8));
    }

    uint8_t MMU::readIF() const
    {
        // Direct register access - no bus cycle. Used by CPU interrupt logic
        // and Timer interrupt posting, neither of which is a bus access.
        return m_ifReg | 0xE0u;
    }

    void MMU::writeIF(uint8_t v)
    {
        // Direct register access - no bus cycle.
        m_ifReg = v & 0x1Fu;
    }

    uint8_t MMU::readIE() const
    {
        if (m_testRam)
            return m_testRam[ADDR_IE];
        return m_ie;
    }

    void MMU::writeIE(uint8_t v)
    {
        if (m_testRam)
        {
            m_testRam[ADDR_IE] = v;
            return;
        }
        m_ie = v;
    }

    uint8_t MMU::peek8(uint16_t addr) const
    {
        // Same routing as read8 but no cycle callback and const-safe.
        if (m_testRam)
            return m_testRam[addr];

        if (addr <= ADDR_ROM_END)
            return m_cart ? m_cart->read(addr) : 0xFFu;
        if (addr >= 0x8000u && addr <= 0x9FFFu)
            return m_ppu ? m_ppu->peekVRAM(addr) : 0xFFu;
        if (addr >= ADDR_ERAM_BASE && addr <= ADDR_ERAM_END)
            return m_cart ? m_cart->read(addr) : 0xFFu;
        if (addr >= 0xC000u && addr <= 0xCFFFu)
            return m_wram[addr - 0xC000u];
        if (addr >= 0xD000u && addr <= 0xDFFFu)
            return m_wram[wramBankOffset(m_svbk) + (addr - 0xD000u)];
        // Echo RAM - PanDocs.2
        if (addr >= 0xE000u && addr <= 0xEFFFu)
            return m_wram[addr - 0xE000u];
        if (addr >= 0xF000u && addr <= 0xFDFFu)
            return m_wram[wramBankOffset(m_svbk) + (addr - 0xF000u)];
        if (addr >= 0xFE00u && addr <= 0xFE9Fu)
            return m_ppu ? m_ppu->peekOAM(addr) : 0xFFu;
        // Prohibited area: 0xFEA0–0xFEFF returns 0x00 on DMG - PanDocs.2
        if (addr >= 0xFEA0u && addr <= 0xFEFFu)
            return 0x00u;
        if (addr == ADDR_IF)
            return m_ifReg | 0xE0u;
        if (addr == 0xFF01u)
            return m_sb;
        if (addr == 0xFF02u)
            return m_sc | 0x7Eu;
        // APU registers - PanDocs Audio Registers
        if ((addr >= 0xFF10u && addr <= 0xFF26u) || (addr >= 0xFF30u && addr <= 0xFF3Fu))
            return m_apu ? m_apu->read(addr) : 0xFFu;
        if (addr >= 0xFF40u && addr <= 0xFF4Bu)
            return m_ppu ? m_ppu->read(addr) : 0xFFu;
        if (addr == 0xFF4Du)
            return m_key1 | 0x7Eu;
        if (addr == 0xFF4Fu)
            return m_ppu ? m_ppu->read(addr) : 0xFFu;
        if (addr >= 0xFF51u && addr <= 0xFF55u)
            return m_ppu ? m_ppu->read(addr) : 0xFFu;
        if (addr == 0xFF56u)
            return m_cgbMode ? ((m_rp & 0xC1u) | 0x3Eu) : 0xFFu;
        if (addr >= 0xFF68u && addr <= 0xFF6Bu)
            return m_ppu ? m_ppu->read(addr) : 0xFFu;
        // CGB object priority mode
        if (addr == 0xFF6Cu)
            return m_cgbMode ? ((m_opri & 0x01u) | 0xFEu) : 0xFFu;
        if (addr == 0xFF70u)
            return m_svbk | 0xF8u;
        if (addr >= ADDR_HRAM_BASE && addr <= ADDR_HRAM_END)
            return m_hram[addr - ADDR_HRAM_BASE];
        if (addr == ADDR_IE)
            return m_ie;
        return 0xFFu;
    }

    void MMU::triggerOAMCorrupt(uint16_t addr, OAMCorruptType type)
    {
        // PanDocs.25 OAM Corruption Bug - only fires when addr is in the OAM region
        if (m_ppu && addr >= 0xFE00u && addr <= 0xFEFFu)
            m_ppu->triggerOAMCorrupt(type);
    }

    void MMU::resetTimerDIV()
    {
        if (m_timer) m_timer->resetDIV();
    }

}
