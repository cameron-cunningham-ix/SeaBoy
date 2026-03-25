#include "GameBoy.hpp"
#include "SaveState.hpp"

#include <cstdio>
#include <cstring>
#ifndef PICO_BUILD
#include <fstream>
#include <vector>
#endif

namespace SeaBoy
{
    GameBoy::GameBoy()
        : m_mmu()
        , m_cpu(m_mmu)      // CPU holds a reference to m_mmu - safe: both live in GameBoy
        , m_timer(m_mmu)    // Timer holds a reference to m_mmu for interrupt flag writes
        , m_ppu(m_mmu)      // PPU holds a reference to m_mmu for interrupt flag writes
        , m_apu(m_mmu)      // APU holds a reference to m_mmu (for future interrupt use)
    {
        // Wire Timer, PPU, APU, and Joypad into MMU so I/O registers route through them.
        m_mmu.setTimer(&m_timer);
        m_mmu.setPPU(&m_ppu);
        m_mmu.setAPU(&m_apu);
        m_mmu.setJoypad(&m_joypad);

        // Joypad interrupt: set IF bit 4 (INT_JOYPAD) on any button press.
        m_joypad.setIFCallback(&GameBoy::onJoypadIRQ, this);

        // M-cycle callback: tick subsystems after every bus access / internal cycle.
        m_mmu.setCycleCallback(&GameBoy::onBusCycle, this);

        // Watch callback: fired from MMU when a data breakpoint matches.
        m_mmu.setWatchCallback(&GameBoy::onWatchHit, this);

        // CPU event callback: forwards CPU-level events to the UI event log.
        m_cpu.setCPUEventCallback(&GameBoy::onCPUEvent, this);

        m_cpu.reset();
        m_timer.reset();
        m_ppu.reset();
        m_apu.reset();
    }

    bool GameBoy::loadROM(const std::string& path)
    {
#ifdef PICO_BUILD
        // On Pico, ROMs are loaded via mmu().loadROM(data, size) directly from SD card.
        // This string-path overload is unused; call sites use the MMU API instead.
        (void)path;
        return false;
#else
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            std::fprintf(stderr, "GameBoy: failed to open ROM: %s\n", path.c_str());
            return false;
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(data.data()), size))
        {
            std::fprintf(stderr, "GameBoy: failed to read ROM: %s\n", path.c_str());
            return false;
        }

        m_mmu.reset();
        m_mmu.loadROM(data.data(), data.size());

        // PanDocs.10 - CGB flag at header byte 0x0143
        // 0x80 = CGB+DMG compatible, 0xC0 = CGB only
        if (m_modeOverride == HardwareMode::CGB) {
            m_cgbMode = true;
        } else if (m_modeOverride == HardwareMode::DMG) {
            m_cgbMode = false;
        } else {
            uint8_t cgbFlag = (data.size() > 0x0143u) ? data[0x0143] : 0x00;
            m_cgbMode = (cgbFlag == 0x80 || cgbFlag == 0xC0);
        }

        m_mmu.setCGBMode(m_cgbMode);

        uint8_t headerChecksum = (data.size() > 0x014Du) ? data[0x014D] : 0x00;
        m_cpu.reset(m_cgbMode, headerChecksum);
        m_timer.reset();
        m_ppu.reset(m_cgbMode);
        m_apu.reset();

        m_romPath = path;

        // Auto-load .sav file if the cartridge has battery backup
        if (SaveFile::hasBattery(data.data(), data.size()))
        {
            std::string savPath = SaveFile::getSavePath(path);
            SaveFile::load(*this, savPath);
        }

        return true;
#endif
    }

    uint32_t GameBoy::tick()
    {
        const uint16_t pc = m_cpu.registers().PC;
        // Latch current PC so WatchHit carries the instruction that triggered the watch.
        m_mmu.setWatchPC(pc);

        // Fire execution history callback with pre-step snapshot (zero cost when null).
        if (m_execFn)
        {
            ExecSnapshot snap;
            snap.pc     = pc;
            snap.opcode = m_mmu.peek8(pc);
            snap.regs   = m_cpu.registers();
            snap.ime    = m_cpu.ime();
            snap.halted = m_cpu.halted();
            m_execFn(m_execCtx, snap);
        }

        // Subsystems (timer, PPU, APU) are ticked at M-cycle granularity
        // via the onBusCycle callback during cpu.step(), not after.
        const uint8_t ifBefore = m_eventFn ? m_mmu.readIF() : m_prevIF;
        const uint32_t cycles = m_cpu.step();
        m_totalCycles += cycles;

        // Fire IntRequested for each IF bit newly set during this tick.
        if (m_eventFn)
        {
            const uint8_t ifAfter = m_mmu.readIF();
            uint8_t newBits = static_cast<uint8_t>(ifAfter & ~ifBefore & 0x1Fu);
            for (uint8_t b = 0; b < 5; ++b)
            {
                if (newBits & (1u << b))
                {
                    GameEvent ev{};
                    ev.cycle  = m_totalCycles;
                    ev.pc     = m_cpu.registers().PC;
                    ev.ie     = m_mmu.readIE();
                    ev.ifReg  = ifAfter;
                    ev.kind   = EventKind::IntRequested;
                    ev.param  = b;
                    m_eventFn(m_eventCtx, ev);
                }
            }
        }

        return cycles;
    }

    void GameBoy::onBusCycle(void* ctx, uint32_t tCycles)
    {
        auto* gb = static_cast<GameBoy*>(ctx);
        // Timer always runs at CPU speed (tCycles unchanged).
        gb->m_timer.tick(tCycles);
        // PanDocs.10 CGB Double Speed Mode: PPU and APU run at fixed rate.
        // In double speed, CPU T-cycles are twice as fast, so halve them for PPU/APU.
        uint32_t ppuCycles = gb->m_mmu.isDoubleSpeed() ? (tCycles / 2) : tCycles;
        gb->m_ppu.tick(ppuCycles);
        gb->m_apu.tick(ppuCycles, gb->m_timer.sysCounter());
    }

    void GameBoy::onWatchHit(void* ctx, const WatchHit& hit)
    {
        auto* gb = static_cast<GameBoy*>(ctx);
        if (!gb->m_watchHitPending) // capture only the first hit per tick
        {
            gb->m_watchHitPending = true;
            gb->m_pendingWatchHit = hit;
        }
    }

    bool GameBoy::consumePendingWatch(WatchHit& out)
    {
        if (!m_watchHitPending) return false;
        out = m_pendingWatchHit;
        m_watchHitPending = false;
        return true;
    }

    void GameBoy::onJoypadIRQ(void* ctx)
    {
        // PanDocs.6 Joypad Input - request joypad interrupt (IF bit 4)
        auto* gb = static_cast<GameBoy*>(ctx);
        gb->m_mmu.writeIF(gb->m_mmu.readIF() | 0x10u);
    }

    bool GameBoy::saveState(const std::string& path) const
    {
        return SaveState::save(*this, path);
    }

    bool GameBoy::loadState(const std::string& path)
    {
        return SaveState::load(*this, path);
    }

    bool GameBoy::saveSRAM(const std::string& path) const
    {
        return SaveFile::save(*this, path);
    }

    bool GameBoy::loadSRAM(const std::string& path)
    {
        return SaveFile::load(*this, path);
    }

    void GameBoy::onCPUEvent(void* ctx, const CPU::CPUEvent& ev)
    {
        auto* gb = static_cast<GameBoy*>(ctx);
        if (!gb->m_eventFn) return;

        GameEvent gev{};
        gev.cycle  = gb->m_totalCycles;
        gev.pc     = ev.pc;
        gev.ie     = ev.ie;
        gev.ifReg  = ev.ifReg;
        gev.param  = ev.param;

        switch (ev.kind)
        {
            case CPU::CPUEventKind::ISREntry:    gev.kind = EventKind::IntServiced; break;
            case CPU::CPUEventKind::IMEEnabled:  gev.kind = EventKind::IMEEnabled;  break;
            case CPU::CPUEventKind::IMEDisabled: gev.kind = EventKind::IMEDisabled; break;
            case CPU::CPUEventKind::HaltEnter:   gev.kind = EventKind::HaltEnter;   break;
            case CPU::CPUEventKind::HaltExit:    gev.kind = EventKind::HaltExit;    break;
            case CPU::CPUEventKind::RETI:        gev.kind = EventKind::RETI;        break;
        }

        gb->m_eventFn(gb->m_eventCtx, gev);
    }

}
