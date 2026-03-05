#include "GameBoy.hpp"

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
    {
        // Wire Timer and PPU into MMU so I/O registers route through them.
        m_mmu.setTimer(&m_timer);
        m_mmu.setPPU(&m_ppu);

        // M-cycle callback: tick subsystems after every bus access / internal cycle.
        m_mmu.setCycleCallback(&GameBoy::onBusCycle, this);

        m_cpu.reset();
        m_timer.reset();
        m_ppu.reset();
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
        m_cpu.reset();
        m_timer.reset();
        m_ppu.reset();

        return true;
    }

    uint32_t GameBoy::tick()
    {
        // Subsystems (timer, PPU, and future APU) are ticked at M-cycle granularity
        // via the onBusCycle callback during cpu.step(), not after.
        return m_cpu.step();
    }

    void GameBoy::onBusCycle(void* ctx, uint32_t tCycles)
    {
        auto* gb = static_cast<GameBoy*>(ctx);
        gb->m_timer.tick(tCycles);
        gb->m_ppu.tick(tCycles);
        // future: gb->m_apu.tick(tCycles);
    }

}
