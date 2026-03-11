#include "GameBoy.hpp"
#include "SaveState.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

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

        m_cpu.reset();
        m_timer.reset();
        m_ppu.reset();
        m_apu.reset();
    }

    bool GameBoy::loadROM(const std::string& path)
    {
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
        uint8_t cgbFlag = (data.size() > 0x0143u) ? data[0x0143] : 0x00;
        m_cgbMode = (cgbFlag == 0x80 || cgbFlag == 0xC0);

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
    }

    uint32_t GameBoy::tick()
    {
        // Subsystems (timer, PPU, APU) are ticked at M-cycle granularity
        // via the onBusCycle callback during cpu.step(), not after.
        return m_cpu.step();
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

}
