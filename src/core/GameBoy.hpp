#pragma once
#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <cstdint>
#include <string>

#include "APU.hpp"
#include "CPU.hpp"
#include "Joypad.hpp"
#include "MMU.hpp"
#include "PPU.hpp"
#include "Timer.hpp"

// GameBoy - top-level orchestrator
// Owns all hardware components by value; drives the emulation loop.
// UIPlatform interacts only through tick() and getFrameBuffer().
// PanDocs.1 - System overview

namespace SeaBoy
{
    // T-cycles per frame: 154 lines × 456 T-cycles/line
    // PanDocs.4.5 - LCD Status Registers & PanDocs.4.8 Rendering
    constexpr uint32_t TCYCLES_PER_FRAME = 70224;

    // Hardware model override for ROM loading.
    // Auto = detect from ROM header (0x0143); DMG/CGB = force that model.
    enum class HardwareMode { Auto, DMG, CGB };

    class GameBoy
    {
    public:
        GameBoy();

        // Load a ROM file from disk. Returns true on success.
        bool loadROM(const std::string& path);

        // Execute one instruction and advance all subsystems by the resulting T-cycles.
        // Returns T-cycles consumed. Caller accumulates until TCYCLES_PER_FRAME.
        [[nodiscard]] uint32_t tick();

        // Returns the 160×144 RGBA framebuffer produced by the PPU.
        // Pointer is valid for the lifetime of this GameBoy instance.
        [[nodiscard]] const uint32_t* getFrameBuffer() const { return m_ppu.frameBuffer(); }

        // Accumulated serial port output - used by blargg_runner to detect pass/fail.
        [[nodiscard]] const std::string& serialOutput() const { return m_mmu.serialOutput(); }

        // Joypad input - called by UIPlatform on each SDL key event.
        void setButton(Button btn, bool pressed) { m_joypad.setButton(btn, pressed); }

        // Debug access to CPU registers
        [[nodiscard]] const CPU& cpu() const { return m_cpu; }

        // Debug access to MMU (for peek8 etc.)
        [[nodiscard]] const MMU& mmu() const { return m_mmu; }

        // Debug access to PPU state (registers, VRAM, OAM)
        [[nodiscard]] const PPU& ppu() const { return m_ppu; }
        [[nodiscard]] PPU& ppu() { return m_ppu; }

        // Debug access to Timer state (DIV, TIMA, TMA, TAC)
        [[nodiscard]] const Timer& timer() const { return m_timer; }

        // Audio access - used by main.cpp to drain samples for SDL audio.
        [[nodiscard]] APU& apu() { return m_apu; }

        // CGB mode flag - true if the loaded ROM has CGB flag 0x80 or 0xC0.
        [[nodiscard]] bool isCGB() const { return m_cgbMode; }

        // Hardware model override - takes effect on next loadROM().
        void setHardwareMode(HardwareMode mode) { m_modeOverride = mode; }
        [[nodiscard]] HardwareMode hardwareMode() const { return m_modeOverride; }

        // Joypad access (const)
        [[nodiscard]] const Joypad& joypad() const { return m_joypad; }

        // ROM path of currently loaded ROM.
        [[nodiscard]] const std::string& romPath() const { return m_romPath; }

        // Save state: snapshot full emulator state to file.
        bool saveState(const std::string& path) const;
        bool loadState(const std::string& path);

        // Save file: persist/restore battery-backed SRAM.
        bool saveSRAM(const std::string& path) const;
        bool loadSRAM(const std::string& path);

        // Mutable accessors for save state deserialization (friend-like access).
        CPU&    cpuMut()    { return m_cpu; }
        MMU&    mmuMut()    { return m_mmu; }
        PPU&    ppuMut()    { return m_ppu; }
        Timer&  timerMut()  { return m_timer; }
        Joypad& joypadMut() { return m_joypad; }

    private:
        // Member declaration order determines construction order.
        // MMU must be constructed before CPU, Timer, PPU, and APU (all hold MMU&).
        MMU    m_mmu;
        CPU    m_cpu;
        Timer  m_timer;
        PPU    m_ppu;
        APU    m_apu;
        Joypad m_joypad;
        bool         m_cgbMode = false;
        HardwareMode m_modeOverride = HardwareMode::Auto;
        std::string m_romPath;

        // M-cycle callback - ticks timer and PPU during CPU execution.
        static void onBusCycle(void* ctx, uint32_t tCycles);

        // Joypad interrupt callback - sets IF bit 4 on button press.
        static void onJoypadIRQ(void* ctx);
    };

}

#endif
