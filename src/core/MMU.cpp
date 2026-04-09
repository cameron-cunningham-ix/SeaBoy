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

    void MMU::loadROM(std::vector<uint8_t>&& rom)
    {
        m_cart = Cartridge::create(std::move(rom));
    }

    void MMU::enableTestMode()
    {
        m_testRam = std::make_unique<uint8_t[]>(65536);
        std::memset(m_testRam.get(), 0x00, 65536);
        m_ifReg = 0; // no pending interrupts; IF must not alias test RAM at 0xFF0F
    }

    // PanDocs.2 SVBK - CGB WRAM bank offset for 0xD000-0xDFFF region.
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

        // PanDocs OAM DMA: the CPU loses access only to the bus the DMA controller
        // is using. On CGB, cartridge/SRAM and WRAM are on separate buses, so only
        // the bus matching the DMA source is locked. VRAM-sourced DMA locks the VRAM
        // bus ($8000-$9FFF); ROM/WRAM/ERAM-sourced DMA locks the external bus.
        // PanDocs says DMG is "HRAM only" but mooneye add_sp_e_timing needs WRAM
        // to be readable on DMG too. OAM ($FE00-$FE9F) is always locked during DMA
        // (handled in PPU::readOAM).
        if (isDMAActive() && addr < 0xFE00u)
        {
            const uint16_t src       = m_ppu ? m_ppu->dmaSource() : 0u;
            const bool srcIsVRAM     = (src  >= 0x8000u && src  <= 0x9FFFu);
            const bool addrIsVRAM    = (addr >= 0x8000u && addr <= 0x9FFFu);
            if (srcIsVRAM == addrIsVRAM) // same bus -> conflict
            {
                if (m_cycleFn) m_cycleFn(m_cycleCtx, 4);
                return 0xFFu;
            }
        }

        uint8_t val = 0xFFu; // Open bus default

        // PanDocs.2 - Memory Map routing.
        // Outer switch dispatches on the page (addr >> 8), letting the compiler build a
        // 256-entry jump table: O(1) dispatch regardless of address region.
        switch (addr >> 8)
        {
            // 0x8000-0x9FFF: VRAM - routed to PPU
            case 0x80: case 0x81: case 0x82: case 0x83:
            case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x8C: case 0x8D: case 0x8E: case 0x8F:
            case 0x90: case 0x91: case 0x92: case 0x93:
            case 0x94: case 0x95: case 0x96: case 0x97:
            case 0x98: case 0x99: case 0x9A: case 0x9B:
            case 0x9C: case 0x9D: case 0x9E: case 0x9F:
                val = m_ppu ? m_ppu->readVRAM(addr) : 0xFFu;
                break;

            // 0xA000-0xBFFF: external RAM - delegated to Cartridge
            case 0xA0: case 0xA1: case 0xA2: case 0xA3:
            case 0xA4: case 0xA5: case 0xA6: case 0xA7:
            case 0xA8: case 0xA9: case 0xAA: case 0xAB:
            case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            case 0xB0: case 0xB1: case 0xB2: case 0xB3:
            case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                val = m_cart ? m_cart->read(addr) : 0xFFu;
                break;

            // 0xC000-0xCFFF: WRAM bank 0 - PanDocs.2
            case 0xC0: case 0xC1: case 0xC2: case 0xC3:
            case 0xC4: case 0xC5: case 0xC6: case 0xC7:
            case 0xC8: case 0xC9: case 0xCA: case 0xCB:
            case 0xCC: case 0xCD: case 0xCE: case 0xCF:
                val = m_wram[addr - 0xC000u];
                break;

            // 0xD000-0xDFFF: WRAM switchable (SVBK) - PanDocs.2
            case 0xD0: case 0xD1: case 0xD2: case 0xD3:
            case 0xD4: case 0xD5: case 0xD6: case 0xD7:
            case 0xD8: case 0xD9: case 0xDA: case 0xDB:
            case 0xDC: case 0xDD: case 0xDE: case 0xDF:
                val = m_wram[wramBankOffset(m_svbk) + (addr - 0xD000u)];
                break;

            // 0xE000-0xEFFF: Echo RAM bank 0 - PanDocs.2
            case 0xE0: case 0xE1: case 0xE2: case 0xE3:
            case 0xE4: case 0xE5: case 0xE6: case 0xE7:
            case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            case 0xEC: case 0xED: case 0xEE: case 0xEF:
                val = m_wram[addr - 0xE000u];
                break;

            // 0xF000-0xFDFF: Echo RAM switchable - PanDocs.2
            case 0xF0: case 0xF1: case 0xF2: case 0xF3:
            case 0xF4: case 0xF5: case 0xF6: case 0xF7:
            case 0xF8: case 0xF9: case 0xFA: case 0xFB:
            case 0xFC: case 0xFD:
                val = m_wram[wramBankOffset(m_svbk) + (addr - 0xF000u)];
                break;

            // 0xFE00-0xFEFF: OAM + prohibited area - PanDocs.2
            case 0xFE:
                if (addr <= 0xFE9Fu)
                    val = m_ppu ? m_ppu->readOAM(addr) : 0xFFu;
                else
                    val = 0x00u; // 0xFEA0-0xFEFF prohibited, reads as 0x00 on DMG
                break;

            // 0xFF00-0xFFFF: I/O registers, HRAM, IE - PanDocs.2
            case 0xFF:
            {
                const uint8_t lo = static_cast<uint8_t>(addr);
                // HRAM (0xFF80-0xFFFE) - checked before I/O switch; stack accesses land here
                if (lo >= 0x80u && lo < 0xFFu)
                {
                    val = m_hram[lo - 0x80u];
                    break;
                }
                // Inner switch on I/O register low byte - also a jump table
                switch (lo)
                {
                    // Joypad P1 - PanDocs.6
                    case 0x00: val = m_joypad ? m_joypad->read() : 0xFFu; break;
                    // Serial - PanDocs.7
                    case 0x01: val = m_sb; break;
                    case 0x02: val = m_sc | 0x7Eu; break; // unused bits read as 1
                    // Timer - PanDocs.8
                    case 0x04: case 0x05: case 0x06: case 0x07:
                        val = m_timer ? m_timer->read(addr) : 0xFFu; break;
                    // IF - PanDocs.9
                    case 0x0F: val = m_ifReg | 0xE0u; break; // upper 3 bits always 1
                    // APU NR registers 0xFF10-0xFF26 - PanDocs Audio
                    case 0x10: case 0x11: case 0x12: case 0x13:
                    case 0x14: case 0x15: case 0x16: case 0x17:
                    case 0x18: case 0x19: case 0x1A: case 0x1B:
                    case 0x1C: case 0x1D: case 0x1E: case 0x1F:
                    case 0x20: case 0x21: case 0x22: case 0x23:
                    case 0x24: case 0x25: case 0x26:
                    // APU wave RAM 0xFF30-0xFF3F
                    case 0x30: case 0x31: case 0x32: case 0x33:
                    case 0x34: case 0x35: case 0x36: case 0x37:
                    case 0x38: case 0x39: case 0x3A: case 0x3B:
                    case 0x3C: case 0x3D: case 0x3E: case 0x3F:
                        val = m_apu ? m_apu->read(addr) : 0xFFu; break;
                    // LCD registers 0xFF40-0xFF4B - PanDocs.4
                    case 0x40: case 0x41: case 0x42: case 0x43:
                    case 0x44: case 0x45: case 0x46: case 0x47:
                    case 0x48: case 0x49: case 0x4A: case 0x4B:
                        val = m_ppu ? m_ppu->read(addr) : 0xFFu; break;
                    // CGB speed switching KEY1 - PanDocs.10
                    case 0x4D: val = m_cgbMode ? (m_key1 | 0x7Eu) : 0xFFu; break;
                    // CGB VRAM bank select - PanDocs.2
                    case 0x4F: val = (m_cgbMode && m_ppu) ? m_ppu->read(addr) : 0xFFu; break;
                    // CGB HDMA registers 0xFF51-0xFF55 - PanDocs.10
                    case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
                        val = (m_cgbMode && m_ppu) ? m_ppu->read(addr) : 0xFFu; break;
                    // CGB IR port - bits 2-5 always 1
                    case 0x56: val = m_cgbMode ? ((m_rp & 0xC1u) | 0x3Eu) : 0xFFu; break;
                    // CGB palette registers 0xFF68-0xFF6B - PanDocs.4.7
                    case 0x68: case 0x69: case 0x6A: case 0x6B:
                        val = (m_cgbMode && m_ppu) ? m_ppu->read(addr) : 0xFFu; break;
                    // CGB object priority mode 0xFF6C
                    case 0x6C: val = m_cgbMode ? ((m_opri & 0x01u) | 0xFEu) : 0xFFu; break;
                    // CGB WRAM bank select SVBK 0xFF70 - PanDocs.2
                    case 0x70: val = m_cgbMode ? (m_svbk | 0xF8u) : 0xFFu; break;
                    // IE 0xFFFF
                    case 0xFF: val = m_ie; break;
                    default: break; // open bus for unmapped I/O
                }
                break;
            }

            // 0x0000-0x7FFF: ROM (default; largest range, handled last in source
            // but first in jump table lookup)
            default:
                val = m_cart ? m_cart->read(addr) : 0xFFu;
                break;
        }

#if !defined(PICO_BUILD)
        checkWatch(addr, WatchType::Read, val);
#endif
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

#if !defined(PICO_BUILD)
        // Capture previous value for write-trace before any routing modifies memory.
        const uint8_t prevVal = m_writeFn ? peek8(addr) : 0u;
#endif

        // PanDocs.2 - Memory Map routing (write path).
        // Same page-dispatch approach as read8; builds a 256-entry jump table.
        switch (addr >> 8)
        {
            // 0x8000-0x9FFF: VRAM - routed to PPU
            case 0x80: case 0x81: case 0x82: case 0x83:
            case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x8C: case 0x8D: case 0x8E: case 0x8F:
            case 0x90: case 0x91: case 0x92: case 0x93:
            case 0x94: case 0x95: case 0x96: case 0x97:
            case 0x98: case 0x99: case 0x9A: case 0x9B:
            case 0x9C: case 0x9D: case 0x9E: case 0x9F:
                if (m_ppu) m_ppu->writeVRAM(addr, val);
                break;

            // 0xA000-0xBFFF: external RAM - delegated to Cartridge
            case 0xA0: case 0xA1: case 0xA2: case 0xA3:
            case 0xA4: case 0xA5: case 0xA6: case 0xA7:
            case 0xA8: case 0xA9: case 0xAA: case 0xAB:
            case 0xAC: case 0xAD: case 0xAE: case 0xAF:
            case 0xB0: case 0xB1: case 0xB2: case 0xB3:
            case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            case 0xB8: case 0xB9: case 0xBA: case 0xBB:
            case 0xBC: case 0xBD: case 0xBE: case 0xBF:
                if (m_cart) m_cart->write(addr, val);
                break;

            // 0xC000-0xCFFF: WRAM bank 0 - PanDocs.2
            case 0xC0: case 0xC1: case 0xC2: case 0xC3:
            case 0xC4: case 0xC5: case 0xC6: case 0xC7:
            case 0xC8: case 0xC9: case 0xCA: case 0xCB:
            case 0xCC: case 0xCD: case 0xCE: case 0xCF:
                m_wram[addr - 0xC000u] = val;
                break;

            // 0xD000-0xDFFF: WRAM switchable (SVBK) - PanDocs.2
            case 0xD0: case 0xD1: case 0xD2: case 0xD3:
            case 0xD4: case 0xD5: case 0xD6: case 0xD7:
            case 0xD8: case 0xD9: case 0xDA: case 0xDB:
            case 0xDC: case 0xDD: case 0xDE: case 0xDF:
                m_wram[wramBankOffset(m_svbk) + (addr - 0xD000u)] = val;
                break;

            // 0xE000-0xEFFF: Echo RAM bank 0 - PanDocs.2
            case 0xE0: case 0xE1: case 0xE2: case 0xE3:
            case 0xE4: case 0xE5: case 0xE6: case 0xE7:
            case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            case 0xEC: case 0xED: case 0xEE: case 0xEF:
                m_wram[addr - 0xE000u] = val;
                break;

            // 0xF000-0xFDFF: Echo RAM switchable - PanDocs.2
            case 0xF0: case 0xF1: case 0xF2: case 0xF3:
            case 0xF4: case 0xF5: case 0xF6: case 0xF7:
            case 0xF8: case 0xF9: case 0xFA: case 0xFB:
            case 0xFC: case 0xFD:
                m_wram[wramBankOffset(m_svbk) + (addr - 0xF000u)] = val;
                break;

            // 0xFE00-0xFEFF: OAM + prohibited area - PanDocs.2
            case 0xFE:
                if (addr <= 0xFE9Fu) { if (m_ppu) m_ppu->writeOAM(addr, val); }
                // 0xFEA0-0xFEFF: writes ignored on DMG
                break;

            // 0xFF00-0xFFFF: I/O registers, HRAM, IE - PanDocs.2
            case 0xFF:
            {
                const uint8_t lo = static_cast<uint8_t>(addr);
                // HRAM (0xFF80-0xFFFE) - checked before I/O switch
                if (lo >= 0x80u && lo < 0xFFu)
                {
                    m_hram[lo - 0x80u] = val;
                    break;
                }
                switch (lo)
                {
                    // Joypad P1 - PanDocs.6
                    case 0x00: if (m_joypad) m_joypad->write(val); break;
                    // Serial port - PanDocs.7
                    case 0x01: m_sb = val; break;
                    case 0x02:
                        m_sc = val;
                        if (val & 0x80u) // transfer start (internal clock)
                        {
                            m_serialOutput += static_cast<char>(m_sb);
                            m_sc &= ~0x80u; // clear start bit - transfer "instant"
                        }
                        break;
                    // Timer 0xFF04-0xFF07 - PanDocs.8
                    case 0x04: case 0x05: case 0x06: case 0x07:
                        if (m_timer) m_timer->write(addr, val); break;
                    // IF 0xFF0F - PanDocs.9
                    case 0x0F: m_ifReg = val & 0x1Fu; break; // only lower 5 bits writable
                    // APU NR registers 0xFF10-0xFF26 - PanDocs Audio
                    case 0x10: case 0x11: case 0x12: case 0x13:
                    case 0x14: case 0x15: case 0x16: case 0x17:
                    case 0x18: case 0x19: case 0x1A: case 0x1B:
                    case 0x1C: case 0x1D: case 0x1E: case 0x1F:
                    case 0x20: case 0x21: case 0x22: case 0x23:
                    case 0x24: case 0x25: case 0x26:
                    // APU wave RAM 0xFF30-0xFF3F
                    case 0x30: case 0x31: case 0x32: case 0x33:
                    case 0x34: case 0x35: case 0x36: case 0x37:
                    case 0x38: case 0x39: case 0x3A: case 0x3B:
                    case 0x3C: case 0x3D: case 0x3E: case 0x3F:
                        if (m_apu) m_apu->write(addr, val); break;
                    // LCD registers 0xFF40-0xFF4B - PanDocs.4
                    case 0x40: case 0x41: case 0x42: case 0x43:
                    case 0x44: case 0x45: case 0x46: case 0x47:
                    case 0x48: case 0x49: case 0x4A: case 0x4B:
                        if (m_ppu) m_ppu->write(addr, val); break;
                    // CGB speed switching KEY1 - PanDocs.10
                    case 0x4D:
                        if (m_cgbMode) m_key1 = (m_key1 & 0x80u) | (val & 0x01u); break;
                    // CGB VRAM bank select 0xFF4F - PanDocs.2
                    case 0x4F: if (m_ppu) m_ppu->write(addr, val); break;
                    // CGB HDMA registers 0xFF51-0xFF55 - PanDocs.10
                    case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
                        if (m_ppu) m_ppu->write(addr, val); break;
                    // CGB IR port
                    case 0x56: if (m_cgbMode) m_rp = val & 0xC1u; break;
                    // CGB palette registers 0xFF68-0xFF6B - PanDocs.4.7
                    case 0x68: case 0x69: case 0x6A: case 0x6B:
                        if (m_ppu) m_ppu->write(addr, val); break;
                    // CGB object priority mode 0xFF6C
                    case 0x6C: if (m_cgbMode) m_opri = val & 0x01u; break;
                    // CGB WRAM bank select SVBK 0xFF70 - PanDocs.2
                    case 0x70: if (m_cgbMode) m_svbk = val & 0x07u; break;
                    // IE 0xFFFF
                    case 0xFF: m_ie = val; break;
                    default: break; // writes to unmapped I/O silently ignored
                }
                break;
            }

            // 0x0000-0x7FFF: ROM MBC register writes - delegated to Cartridge
            default:
                if (m_cart) m_cart->write(addr, val);
                break;
        }

#if !defined(PICO_BUILD)
        if (m_writeFn) m_writeFn(m_writeCtx, addr, prevVal, val);
        checkWatch(addr, WatchType::Write, val);
#endif
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
        // Prohibited area: 0xFEA0-0xFEFF returns 0x00 on DMG - PanDocs.2
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

#if !defined(PICO_BUILD)
    // -- Watchpoint implementation (desktop/debugger only) ----------------

    void MMU::checkWatch(uint16_t addr, WatchType accessType, uint8_t value)
    {
        if (!m_watchFn || m_watchpoints.empty()) return;
        for (const auto& wp : m_watchpoints)
        {
            if (wp.addr == addr &&
                (static_cast<uint8_t>(wp.type) & static_cast<uint8_t>(accessType)) != 0)
            {
                m_watchFn(m_watchCtx, WatchHit{ addr, accessType, value, m_watchPC });
                return; // fire once per access even if multiple watchpoints match
            }
        }
    }

    void MMU::addWatchpoint(const Watchpoint& wp)
    {
        for (auto& e : m_watchpoints)
        {
            if (e.addr == wp.addr) { e.type = wp.type; return; } // update type if exists
        }
        m_watchpoints.push_back(wp);
    }

    void MMU::removeWatchpoint(uint16_t addr)
    {
        m_watchpoints.erase(
            std::remove_if(m_watchpoints.begin(), m_watchpoints.end(),
                [addr](const Watchpoint& w) { return w.addr == addr; }),
            m_watchpoints.end());
    }

    void MMU::clearWatchpoints()
    {
        m_watchpoints.clear();
    }
#endif // !PICO_BUILD

}
