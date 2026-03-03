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
    {
        // Wire Timer into MMU so 0xFF04–0xFF07 route through it.
        m_mmu.setTimer(&m_timer);

        m_cpu.reset();
        m_timer.reset();
        std::memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
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

        return true;
    }

    uint32_t GameBoy::tick()
    {
        uint32_t cycles = m_cpu.step();

        m_timer.tick(cycles);
        // TODO: ppu.tick(cycles);
        // TODO: apu.tick(cycles);

        return cycles;
    }

} // namespace SeaBoy
