#pragma once
#ifndef MMU_H
#define MMU_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// PPU.hpp included for OAMCorruptType used in triggerOAMCorrupt().
// PPU.hpp only forward-declares MMU, so there is no circular dependency
#include "PPU.hpp"

// PanDocs.2 - Memory Map
//
// Address space:
//   0x0000-0x7FFF  ROM (cartridge, bank-switched by MBC)
//   0x8000-0x9FFF  VRAM (PPU-owned, routed to PPU::readVRAM/writeVRAM)
//   0xA000-0xBFFF  External RAM (cartridge)
//   0xC000-0xDFFF  WRAM (8 KB)
//   0xE000-0xFDFF  Echo RAM (mirrors WRAM 0xC000-0xDFFF)
//   0xFE00-0xFE9F  OAM (PPU-owned, routed to PPU::readOAM/writeOAM)
//   0xFEA0-0xFEFF  Prohibited (reads 0x00, writes ignored on DMG)
//   0xFF0F         IF - Interrupt Flag
//   0xFF80-0xFFFE  HRAM (127 bytes)
//   0xFFFF         IE - Interrupt Enable
//   all other addresses -> open bus, reads return 0xFF

namespace SeaBoy
{
    // -- Data watchpoint types -------------------------------------------

    enum class WatchType : uint8_t { Read = 1, Write = 2, ReadWrite = 3 };

    struct Watchpoint { uint16_t addr; WatchType type; };

    // Filled by MMU when a watchpoint fires; consumed by the main loop / DebuggerUI.
    struct WatchHit
    {
        uint16_t  addr;   // address accessed
        WatchType type;   // the actual access (Read or Write, never ReadWrite)
        uint8_t   value;  // byte value read or written
        uint16_t  pc;     // CPU PC latched by GameBoy before tick()
    };

    // Named address constants
    constexpr uint16_t ADDR_P1        = 0xFF00; // Joypad / P1 register
    constexpr uint16_t ADDR_ROM_BANK  = 0x3FFF;
    constexpr uint16_t ADDR_ROM_END   = 0x7FFF;
    constexpr uint16_t ADDR_ERAM_BASE = 0xA000;
    constexpr uint16_t ADDR_ERAM_END  = 0xBFFF;
    constexpr uint16_t ADDR_WRAM_BASE = 0xC000;
    constexpr uint16_t ADDR_WRAM_END  = 0xDFFF;
    constexpr uint16_t ADDR_DIV       = 0xFF04; // Timer divider (upper 8 bits of internal counter)
    constexpr uint16_t ADDR_TIMA      = 0xFF05; // Timer counter
    constexpr uint16_t ADDR_TMA       = 0xFF06; // Timer modulo
    constexpr uint16_t ADDR_TAC       = 0xFF07; // Timer control
    constexpr uint16_t ADDR_IF        = 0xFF0F;
    constexpr uint16_t ADDR_HRAM_BASE = 0xFF80;
    constexpr uint16_t ADDR_HRAM_END  = 0xFFFE;
    constexpr uint16_t ADDR_IE        = 0xFFFF;

    // Forward declarations - full definitions included only in their respective .cpp files.
    class Cartridge; // src/cartridge/Cartridge.hpp
    class Timer;     // src/core/Timer.hpp
    class Joypad;    // src/core/Joypad.hpp
    class APU;       // src/core/APU.hpp
    // PPU is fully included above via PPU.hpp

    class MMU
    {
    public:
        MMU();
        // Destructor defined in MMU.cpp where Cartridge is fully visible (unique_ptr rule).
        ~MMU();

        void reset();
        void loadROM(const uint8_t* data, size_t size);
        void loadROM(std::vector<uint8_t>&& rom);

        // Core bus interface
        // Each call represents a 4 T-cycle bus access.
        // If a cycle callback is set, it fires after each access.
        uint8_t read8(uint16_t addr);
        void write8(uint16_t addr, uint8_t val);

        // 16-bit helpers - always little-endian (low byte at addr, high at addr+1)
        // Implemented via two read8/write8 calls; each triggers a cycle callback.
        // PanDocs - LD (nn),SP and PUSH/POP byte order
        uint16_t read16(uint16_t addr);
        void write16(uint16_t addr, uint16_t val);

        // Interrupt register helpers to avoid scattering 0xFF0F / 0xFFFF
        uint8_t readIF() const;
        void writeIF(uint8_t v);
        uint8_t readIE() const;
        void writeIE(uint8_t v);

        // M-cycle callback - set by GameBoy to tick subsystems (timer, PPU, etc.)
        // after every 4 T-cycle bus access or internal cycle.
        using CycleCallback = void(*)(void* ctx, uint32_t tCycles);
        void setCycleCallback(CycleCallback fn, void* ctx) { m_cycleFn = fn; m_cycleCtx = ctx; }

        // Watchpoint callback - fired from read8/write8 when a data breakpoint matches.
        // Same decoupling pattern as CycleCallback: core holds a fn ptr, UI never imported.
        using WatchCallback = void(*)(void* ctx, const WatchHit& hit);
        void setWatchCallback(WatchCallback fn, void* ctx) { m_watchFn = fn; m_watchCtx = ctx; }
        void setWatchPC(uint16_t pc) { m_watchPC = pc; }

        // Write-trace callback - fired from write8 for every write (addr, prevVal, newVal).
        // prevVal is the value read via peek8 before the write completes.
        // Zero cost when null. Does not fire from writeIF/writeIE helpers.
        using WriteTraceCallback = void(*)(void* ctx, uint16_t addr, uint8_t prevVal, uint8_t newVal);
        void setWriteTraceCallback(WriteTraceCallback fn, void* ctx) { m_writeFn = fn; m_writeCtx = ctx; }

        void addWatchpoint(const Watchpoint& wp);
        void removeWatchpoint(uint16_t addr);
        void clearWatchpoints();
        bool hasWatchpoints() const { return !m_watchpoints.empty(); }

        // Tick subsystems without a bus access (for CPU internal M-cycles).
        void tickCycle() { if (m_cycleFn) m_cycleFn(m_cycleCtx, 4); }

        // PPU link - set by GameBoy after constructing both MMU and PPU.
        // MMU routes 0xFF40-0xFF4B to the PPU; null until wired.
        void setPPU(PPU* p) { m_ppu = p; }

        // Returns true while an OAM DMA transfer is in progress (including startup delay).
        // Used to enforce bus conflict: CPU may only access HRAM during DMA
        bool isDMAActive() const;

        // OAM corruption bug - PanDocs.25 OAM Corruption Bug
        // Delegates to PPU::triggerOAMCorrupt() when addr is in 0xFE00-0xFEFF.
        void triggerOAMCorrupt(uint16_t addr, OAMCorruptType type);

        // Serial port output (captured from writes to SB/SC, 0xFF01/02).
        // Blargg test ROMs write results here. Safe to call at any time.
        const std::string& serialOutput() const { return m_serialOutput; }

        // Timer link - set by GameBoy after constructing both MMU and Timer.
        // MMU routes 0xFF04-0xFF07 to the Timer; null until wired.
        void setTimer(Timer* t) { m_timer = t; }
        
        // Reset timer DIV (internal counter) - called by STOP, no cycle side effects.
        void resetTimerDIV();

        // Joypad link - set by GameBoy after constructing both MMU and Joypad.
        // MMU routes 0xFF00 to the Joypad; null until wired.
        void setJoypad(Joypad* j) { m_joypad = j; }

        // APU link - set by GameBoy after constructing both MMU and APU.
        // MMU routes 0xFF10-0xFF26 and 0xFF30-0xFF3F to the APU; null until wired.
        void setAPU(APU* a) { m_apu = a; }

        // CGB mode - enables WRAM banking via SVBK (0xFF70).
        // Called by GameBoy::loadROM() after detecting CGB flag.
        void setCGBMode(bool cgb) { m_cgbMode = cgb; }
        bool isCGBMode() const { return m_cgbMode; }

        // CGB speed switching - PanDocs.10 CGB Double Speed Mode
        // KEY1 (0xFF4D): bit 7 = current speed (0=normal, 1=double), bit 0 = prepare toggle
        bool isDoubleSpeed() const { return (m_key1 & 0x80u) != 0; }
        void toggleSpeed()         { m_key1 ^= 0x80u; m_key1 &= ~0x01u; }

        // Save state serialization
        void serialize(BinaryWriter& w) const;
        void deserialize(BinaryReader& r);

        // Cartridge access for save state / save file.
        Cartridge* cartridge() { return m_cart.get(); }
        const Cartridge* cartridge() const { return m_cart.get(); }

        // Debug: read a byte without triggering the cycle callback.
        // Used by blargg_runner to inspect memory without side effects.
        uint8_t peek8(uint16_t addr) const;

        // Test mode: flat 64 KB overlay, bypasses all normal routing.
        // Used by sm83_runner for single-step CPU tests.
        void enableTestMode();
        bool inTestMode() const { return m_testRam != nullptr; }
        void    testWrite(uint16_t addr, uint8_t val) { m_testRam[addr] = val; }
        uint8_t testRead (uint16_t addr) const       { return m_testRam[addr]; }

    private:
        // Cartridge (ROM + external RAM + MBC state). Null until loadROM() is called.
        std::unique_ptr<Cartridge> m_cart;

        // Timer - null until setTimer() is called by GameBoy. Not owned.
        Timer* m_timer = nullptr;

        // PPU - null until setPPU() is called by GameBoy. Not owned.
        PPU* m_ppu = nullptr;

        // Joypad - null until setJoypad() is called by GameBoy. Not owned.
        Joypad* m_joypad = nullptr;

        // APU - null until setAPU() is called by GameBoy. Not owned.
        APU* m_apu = nullptr;

        // WRAM: 8 KB on DMG (2 banks), 32 KB on CGB (8 banks of 4 KB) - PanDocs.2
        // Always allocated as 32 KB; DMG uses only banks 0+1.
        // 0xC000-0xCFFF: always bank 0. 0xD000-0xDFFF: switchable (SVBK).
        uint8_t m_wram[0x8000]{};
        uint8_t m_hram[0x7F]{};    // 127 bytes HRAM (0xFF80-0xFFFE)
        uint8_t m_ifReg = 0xE1;    // IF - power-on value per PanDocs.22 Power Up Sequence
        uint8_t m_ie    = 0x00;    // IE
        uint8_t m_opri = 0x00;     // 0xFF6C - Object Priority Mode - PanDocs.10
        uint8_t m_rp = 0x00;       // 0xFF56 Infrared Communication port

        // CGB WRAM banking - PanDocs.2 SVBK (0xFF70)
        uint8_t m_svbk    = 0;     // SVBK register (bits 0-2)
        bool    m_cgbMode = false;

        // CGB speed switching - PanDocs.10 KEY1 (0xFF4D)
        uint8_t m_key1 = 0;        // bit 7 = current speed, bit 0 = prepare flag

        // Serial port - PanDocs.7 Serial Data Transfer
        uint8_t     m_sb = 0;      // 0xFF01 SB: transfer data
        uint8_t     m_sc = 0;      // 0xFF02 SC: transfer control
        std::string m_serialOutput;// Accumulated bytes captured from serial transfers

        // Non-null only when test mode is active (heap-allocated to avoid 64 KB stack cost)
        std::unique_ptr<uint8_t[]> m_testRam;

        // M-cycle callback (null until setCycleCallback is called)
        CycleCallback m_cycleFn  = nullptr;
        void*         m_cycleCtx = nullptr;

        // Watchpoints - checked in read8/write8
        std::vector<Watchpoint> m_watchpoints;
        WatchCallback           m_watchFn  = nullptr;
        void*                   m_watchCtx = nullptr;
        uint16_t                m_watchPC  = 0;

        // Write-trace callback
        WriteTraceCallback m_writeFn  = nullptr;
        void*              m_writeCtx = nullptr;

        void checkWatch(uint16_t addr, WatchType accessType, uint8_t value);

    };

}

#endif
