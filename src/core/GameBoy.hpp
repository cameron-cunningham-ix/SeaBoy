#pragma once
#ifndef GAMEBOY_H
#define GAMEBOY_H

#include <cstdint>
#include <string>

#include "CPU.hpp"
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

        // Debug access to CPU registers
        [[nodiscard]] const CPU& cpu() const { return m_cpu; }

        // Debug access to MMU (for peek8 etc.)
        [[nodiscard]] const MMU& mmu() const { return m_mmu; }

    private:
        // Member declaration order determines construction order.
        // MMU must be constructed before CPU, Timer, and PPU (all hold MMU&).
        MMU   m_mmu;
        CPU   m_cpu;
        Timer m_timer;
        PPU   m_ppu;

        // M-cycle callback - ticks timer and PPU during CPU execution.
        static void onBusCycle(void* ctx, uint32_t tCycles);
    };

}

#endif
