#include "SaveState.hpp"
#include "GameBoy.hpp"
#include "../cartridge/Cartridge.hpp"

#include <cstdio>
#include <cstring>
#include <algorithm>

namespace SeaBoy
{
    // ---- ROM title hash (FNV-1a over header bytes 0x0134-0x0143) ----

    uint32_t SaveState::romTitleHash(const uint8_t* rom, size_t romSize)
    {
        uint32_t hash = 0x811C9DC5u; // FNV offset basis
        for (size_t i = 0x0134; i <= 0x0143 && i < romSize; ++i)
        {
            hash ^= rom[i];
            hash *= 0x01000193u; // FNV prime
        }
        return hash;
    }

    // ---- CPU serialize/deserialize ----

    void CPU::serialize(BinaryWriter& w) const
    {
        w.write8(m_regs.A); w.write8(m_regs.F);
        w.write8(m_regs.B); w.write8(m_regs.C);
        w.write8(m_regs.D); w.write8(m_regs.E);
        w.write8(m_regs.H); w.write8(m_regs.L);
        w.write16(m_regs.SP);
        w.write16(m_regs.PC);
        w.writeBool(m_ime);
        w.writeBool(m_halted);
        w.writeBool(m_haltBug);
        w.write8(m_imeDelay);
    }

    void CPU::deserialize(BinaryReader& r)
    {
        m_regs.A = r.read8(); m_regs.F = r.read8();
        m_regs.B = r.read8(); m_regs.C = r.read8();
        m_regs.D = r.read8(); m_regs.E = r.read8();
        m_regs.H = r.read8(); m_regs.L = r.read8();
        m_regs.SP = r.read16();
        m_regs.PC = r.read16();
        m_ime          = r.readBool();
        m_halted       = r.readBool();
        m_haltBug      = r.readBool();
        m_imeDelay = r.read8();
    }

    // ---- Timer serialize/deserialize ----

    void Timer::serialize(BinaryWriter& w) const
    {
        w.write16(m_counter);
        w.write8(m_tima);
        w.write8(m_tma);
        w.write8(m_tac);
        w.writeInt(m_overflowDelay);
    }

    void Timer::deserialize(BinaryReader& r)
    {
        m_counter       = r.read16();
        m_tima          = r.read8();
        m_tma           = r.read8();
        m_tac           = r.read8();
        m_overflowDelay = r.readInt();
    }

    // ---- Joypad serialize/deserialize ----

    void Joypad::serialize(BinaryWriter& w) const
    {
        w.write8(m_select);
        w.write8(m_action);
        w.write8(m_dpad);
        w.write8(m_prevLow);
    }

    void Joypad::deserialize(BinaryReader& r)
    {
        m_select  = r.read8();
        m_action  = r.read8();
        m_dpad    = r.read8();
        m_prevLow = r.read8();
    }

    // ---- MMU serialize/deserialize ----

    void MMU::serialize(BinaryWriter& w) const
    {
        w.writeBlock(m_wram, sizeof(m_wram));
        w.writeBlock(m_hram, sizeof(m_hram));
        w.write8(m_ifReg);
        w.write8(m_ie);
        w.write8(m_svbk);
        w.writeBool(m_cgbMode);
        w.write8(m_key1);
        w.write8(m_sb);
        w.write8(m_sc);
    }

    void MMU::deserialize(BinaryReader& r)
    {
        r.readBlock(m_wram, sizeof(m_wram));
        r.readBlock(m_hram, sizeof(m_hram));
        m_ifReg   = r.read8();
        m_ie      = r.read8();
        m_svbk    = r.read8();
        m_cgbMode = r.readBool();
        m_key1    = r.read8();
        m_sb      = r.read8();
        m_sc      = r.read8();
    }

    // ---- Palettes serialize/deserialize ----

    void Palettes::serialize(BinaryWriter& w) const
    {
        w.write8(m_bgp);
        w.write8(m_obp0);
        w.write8(m_obp1);
        w.writeBlock(m_bcram, sizeof(m_bcram));
        w.writeBlock(m_ocram, sizeof(m_ocram));
        w.write8(m_bcps);
        w.write8(m_ocps);
    }

    void Palettes::deserialize(BinaryReader& r)
    {
        m_bgp  = r.read8();
        m_obp0 = r.read8();
        m_obp1 = r.read8();
        r.readBlock(m_bcram, sizeof(m_bcram));
        r.readBlock(m_ocram, sizeof(m_ocram));
        m_bcps = r.read8();
        m_ocps = r.read8();
    }

    // ---- PixelFetcher serialize/deserialize ----

    void PixelFetcher::serialize(BinaryWriter& w) const
    {
        w.write8(m_bgStep);
        w.write8(m_bgTileX);
        w.write8(m_fetchedTileIndex);
        w.write8(m_fetchedLo);
        w.write8(m_fetchedHi);
        w.write8(m_fetchedAttr);
        w.writeBool(m_inWindow);
        w.writeBool(m_cgbMode);

        // BG FIFO
        for (int i = 0; i < 8; ++i)
        {
            w.write8(m_bgFifo[i].colorID);
            w.write8(m_bgFifo[i].cgbPalette);
            w.writeBool(m_bgFifo[i].cgbPriority);
        }
        w.write8(m_bgFifoHead);
        w.write8(m_bgFifoSize);

        // OBJ FIFO
        for (int i = 0; i < 8; ++i)
        {
            w.write8(m_objFifo[i].colorID);
            w.write8(m_objFifo[i].palette);
            w.writeBool(m_objFifo[i].bgPriority);
            w.writeBool(m_objFifo[i].occupied);
        }
        w.write8(m_objFifoSize);

        // Sprite fetch state
        w.writeBool(m_spriteFetch);
        w.write8(m_objFetchStep);
        w.write8(m_objTileIndex);
        w.write8(m_objSpriteRow);
        w.write8(m_objFetchedLo);
        w.write8(m_objFetchedHi);
        w.write8(m_currentSprite);
        for (int i = 0; i < 10; ++i)
            w.writeBool(m_spriteDone[i]);

        // Pixel output
        w.write8(m_pixelX);
        w.write8(m_discard);
        w.write8(m_initialDelay);
        w.writeBool(m_drewWindow);

        // Register snapshots
        w.write8(m_lcdc);
        w.write8(m_scx);
        w.write8(m_scy);
        w.write8(m_ly);
        w.write8(m_wx);
        w.write8(m_windowLineCounter);
        w.writeBool(m_windowTriggered);
    }

    void PixelFetcher::deserialize(BinaryReader& r)
    {
        m_bgStep           = r.read8();
        m_bgTileX          = r.read8();
        m_fetchedTileIndex = r.read8();
        m_fetchedLo        = r.read8();
        m_fetchedHi        = r.read8();
        m_fetchedAttr      = r.read8();
        m_inWindow         = r.readBool();
        m_cgbMode          = r.readBool();

        for (int i = 0; i < 8; ++i)
        {
            m_bgFifo[i].colorID     = r.read8();
            m_bgFifo[i].cgbPalette  = r.read8();
            m_bgFifo[i].cgbPriority = r.readBool();
        }
        m_bgFifoHead = r.read8();
        m_bgFifoSize = r.read8();

        for (int i = 0; i < 8; ++i)
        {
            m_objFifo[i].colorID    = r.read8();
            m_objFifo[i].palette    = r.read8();
            m_objFifo[i].bgPriority = r.readBool();
            m_objFifo[i].occupied   = r.readBool();
        }
        m_objFifoSize = r.read8();

        m_spriteFetch  = r.readBool();
        m_objFetchStep = r.read8();
        m_objTileIndex = r.read8();
        m_objSpriteRow = r.read8();
        m_objFetchedLo = r.read8();
        m_objFetchedHi = r.read8();
        m_currentSprite = r.read8();
        for (int i = 0; i < 10; ++i)
            m_spriteDone[i] = r.readBool();

        m_pixelX       = r.read8();
        m_discard      = r.read8();
        m_initialDelay = r.read8();
        m_drewWindow   = r.readBool();

        m_lcdc              = r.read8();
        m_scx               = r.read8();
        m_scy               = r.read8();
        m_ly                = r.read8();
        m_wx                = r.read8();
        m_windowLineCounter = r.read8();
        m_windowTriggered   = r.readBool();

        // External pointers (m_vram, m_sprites, m_palettes, m_fbLine) are
        // re-established by PPU when it resumes rendering after state load.
        m_vram        = nullptr;
        m_sprites     = nullptr;
        m_spriteCount = 0;
        m_palettes    = nullptr;
        m_fbLine      = nullptr;
    }

    // ---- PPU serialize/deserialize ----

    void PPU::serialize(BinaryWriter& w) const
    {
        w.writeBlock(m_vram, sizeof(m_vram));
        w.writeBlock(m_oam, sizeof(m_oam));
        w.write8(m_vbk);
        w.writeBool(m_cgbMode);

        // HDMA
        w.write8(m_hdma1);
        w.write8(m_hdma2);
        w.write8(m_hdma3);
        w.write8(m_hdma4);
        w.write8(m_hdma5);
        w.writeBool(m_hdmaActive);
        w.write16(m_hdmaSrc);
        w.write16(m_hdmaDst);
        w.write16(m_hdmaRemain);

        // Mode state
        w.write8(static_cast<uint8_t>(m_mode));
        w.write32(m_lineCycle);
        w.write8(m_ly);

        // LCD registers
        w.write8(m_lcdc);
        w.write8(m_stat);
        w.write8(m_scy);
        w.write8(m_scx);
        w.write8(m_lyc);
        w.write8(m_dma);
        w.write8(m_wy);
        w.write8(m_wx);

        // Window state
        w.writeBool(m_windowTriggered);
        w.write8(m_windowLineCounter);

        // DMA state
        w.writeBool(m_dmaActive);
        w.write8(m_dmaStartDelay);
        w.write16(m_dmaSource);
        w.write8(m_dmaByte);

        // STAT interrupt
        w.writeBool(m_statLine);
        w.write8(static_cast<uint8_t>(m_lycDelay));

        // Framebuffer
        w.writeBlock(m_frameBuffer, sizeof(m_frameBuffer));

        // Sub-objects
        m_palettes.serialize(w);
        m_fetcher.serialize(w);
    }

    void PPU::deserialize(BinaryReader& r)
    {
        r.readBlock(m_vram, sizeof(m_vram));
        r.readBlock(m_oam, sizeof(m_oam));
        m_vbk     = r.read8();
        m_cgbMode = r.readBool();

        m_hdma1      = r.read8();
        m_hdma2      = r.read8();
        m_hdma3      = r.read8();
        m_hdma4      = r.read8();
        m_hdma5      = r.read8();
        m_hdmaActive = r.readBool();
        m_hdmaSrc    = r.read16();
        m_hdmaDst    = r.read16();
        m_hdmaRemain = r.read16();

        m_mode      = static_cast<PPUMode>(r.read8());
        m_lineCycle = r.read32();
        m_ly        = r.read8();

        m_lcdc = r.read8();
        m_stat = r.read8();
        m_scy  = r.read8();
        m_scx  = r.read8();
        m_lyc  = r.read8();
        m_dma  = r.read8();
        m_wy   = r.read8();
        m_wx   = r.read8();

        m_windowTriggered   = r.readBool();
        m_windowLineCounter = r.read8();

        m_dmaActive     = r.readBool();
        m_dmaStartDelay = r.read8();
        m_dmaSource     = r.read16();
        m_dmaByte       = r.read8();

        m_statLine = r.readBool();
        m_lycDelay = static_cast<int8_t>(r.read8());

        r.readBlock(m_frameBuffer, sizeof(m_frameBuffer));

        m_palettes.deserialize(r);
        m_fetcher.deserialize(r);

        // Re-establish PixelFetcher's external pointers after deserialize.
        // If the PPU was in Drawing mode (Mode 3), the fetcher needs valid
        // pointers to VRAM, sprites, palettes, and the framebuffer line.
        // We must NOT call init() here as it resets the mid-scanline state.
        if (m_mode == PPUMode::Drawing)
        {
            uint8_t spriteH = (m_lcdc & 0x04) ? 16 : 8;
            m_oamScan.scan(m_oam, m_ly, spriteH);
            m_fetcher.restorePointers(m_vram,
                                      m_oamScan.sprites(), m_oamScan.count(),
                                      m_palettes,
                                      &m_frameBuffer[m_ly * 160]);
        }
    }

    // ---- APU serialize/deserialize ----

    void APU::serialize(BinaryWriter& w) const
    {
        w.writeBool(m_powered);
        w.write8(m_nr50);
        w.write8(m_nr51);
        w.write8(m_nr10);
        w.write8(m_frameSeqStep);
        w.writeBool(m_prevDivBit);
        w.writeBlock(m_waveRam, sizeof(m_waveRam));

        // CH1
        w.write8(m_ch1.nrx1); w.write8(m_ch1.nrx2);
        w.write8(m_ch1.nrx3); w.write8(m_ch1.nrx4);
        w.writeBool(m_ch1.active);
        w.write8(m_ch1.dutyStep);
        w.write8(m_ch1.lengthTimer);
        w.writeBool(m_ch1.lengthEnable);
        w.write8(m_ch1.volume);
        w.write8(m_ch1.envTimer);
        w.write16(m_ch1.period);
        w.write16(m_ch1.periodTimer);

        // Sweep
        w.write16(m_sweep.shadow);
        w.write8(m_sweep.timer);
        w.writeBool(m_sweep.enabled);
        w.writeBool(m_sweep.negUsed);

        // CH2
        w.write8(m_ch2.nrx1); w.write8(m_ch2.nrx2);
        w.write8(m_ch2.nrx3); w.write8(m_ch2.nrx4);
        w.writeBool(m_ch2.active);
        w.write8(m_ch2.dutyStep);
        w.write8(m_ch2.lengthTimer);
        w.writeBool(m_ch2.lengthEnable);
        w.write8(m_ch2.volume);
        w.write8(m_ch2.envTimer);
        w.write16(m_ch2.period);
        w.write16(m_ch2.periodTimer);

        // CH3
        w.write8(m_ch3.nr30); w.write8(m_ch3.nr32);
        w.write8(m_ch3.nrx3); w.write8(m_ch3.nrx4);
        w.writeBool(m_ch3.active);
        w.write16(m_ch3.lengthTimer);
        w.writeBool(m_ch3.lengthEnable);
        w.write16(m_ch3.period);
        w.write16(m_ch3.periodTimer);
        w.write8(m_ch3.sampleIndex);
        w.write8(m_ch3.sampleBuffer);
        w.write16(m_ch3.ticksUntilNextFetch);

        // CH4
        w.write8(m_ch4.nrx1); w.write8(m_ch4.nrx2);
        w.write8(m_ch4.nr43); w.write8(m_ch4.nrx4);
        w.writeBool(m_ch4.active);
        w.write8(m_ch4.lengthTimer);
        w.writeBool(m_ch4.lengthEnable);
        w.write8(m_ch4.volume);
        w.write8(m_ch4.envTimer);
        w.write16(m_ch4.lfsr);
        w.write32(m_ch4.freqTimer);
    }

    void APU::deserialize(BinaryReader& r)
    {
        m_powered      = r.readBool();
        m_nr50         = r.read8();
        m_nr51         = r.read8();
        m_nr10         = r.read8();
        m_frameSeqStep = r.read8();
        m_prevDivBit   = r.readBool();
        r.readBlock(m_waveRam, sizeof(m_waveRam));

        m_ch1.nrx1         = r.read8(); m_ch1.nrx2 = r.read8();
        m_ch1.nrx3         = r.read8(); m_ch1.nrx4 = r.read8();
        m_ch1.active       = r.readBool();
        m_ch1.dutyStep     = r.read8();
        m_ch1.lengthTimer  = r.read8();
        m_ch1.lengthEnable = r.readBool();
        m_ch1.volume       = r.read8();
        m_ch1.envTimer     = r.read8();
        m_ch1.period       = r.read16();
        m_ch1.periodTimer  = r.read16();

        m_sweep.shadow  = r.read16();
        m_sweep.timer   = r.read8();
        m_sweep.enabled = r.readBool();
        m_sweep.negUsed = r.readBool();

        m_ch2.nrx1         = r.read8(); m_ch2.nrx2 = r.read8();
        m_ch2.nrx3         = r.read8(); m_ch2.nrx4 = r.read8();
        m_ch2.active       = r.readBool();
        m_ch2.dutyStep     = r.read8();
        m_ch2.lengthTimer  = r.read8();
        m_ch2.lengthEnable = r.readBool();
        m_ch2.volume       = r.read8();
        m_ch2.envTimer     = r.read8();
        m_ch2.period       = r.read16();
        m_ch2.periodTimer  = r.read16();

        m_ch3.nr30         = r.read8(); m_ch3.nr32 = r.read8();
        m_ch3.nrx3         = r.read8(); m_ch3.nrx4 = r.read8();
        m_ch3.active       = r.readBool();
        m_ch3.lengthTimer  = r.read16();
        m_ch3.lengthEnable = r.readBool();
        m_ch3.period       = r.read16();
        m_ch3.periodTimer  = r.read16();
        m_ch3.sampleIndex  = r.read8();
        m_ch3.sampleBuffer = r.read8();
        m_ch3.ticksUntilNextFetch = r.read16();

        m_ch4.nrx1         = r.read8(); m_ch4.nrx2 = r.read8();
        m_ch4.nr43         = r.read8(); m_ch4.nrx4 = r.read8();
        m_ch4.active       = r.readBool();
        m_ch4.lengthTimer  = r.read8();
        m_ch4.lengthEnable = r.readBool();
        m_ch4.volume       = r.read8();
        m_ch4.envTimer     = r.read8();
        m_ch4.lfsr         = r.read16();
        m_ch4.freqTimer    = r.read32();

        // Reset audio output pipeline (brief glitch on load is acceptable)
        std::memset(m_sampleBuffer, 0, sizeof(m_sampleBuffer));
        m_sampleWritePos = 0;
        m_sampleReadPos  = 0;
        m_sampleTimer    = 0;
        m_hpfCapLeft     = 0.0;
        m_hpfCapRight    = 0.0;
    }

    // ---- SaveState orchestration ----

    bool SaveState::save(const GameBoy& gb, const std::string& path)
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            std::fprintf(stderr, "SaveState: failed to open for writing: %s\n", path.c_str());
            return false;
        }

        BinaryWriter w(file);

        // Header
        w.writeBlock(SAVE_STATE_MAGIC, 4);
        w.write8(SAVE_STATE_VERSION);

        // ROM hash for validation
        const Cartridge* cart = gb.mmu().cartridge();
        uint32_t hash = cart ? romTitleHash(cart->romData(), cart->romSize()) : 0;
        w.write32(hash);

        // CGB mode flag
        w.writeBool(gb.isCGB());

        // Serialize all components
        gb.cpu().serialize(w);
        gb.timer().serialize(w);
        gb.mmu().serialize(w);
        gb.ppu().serialize(w);

        // APU and Joypad need non-const access via the GameBoy
        // We use const_cast here because serialize is logically const
        const_cast<GameBoy&>(gb).apu().serialize(w);
        gb.joypad().serialize(w);

        // Cartridge MBC state
        if (cart)
            cart->serialize(w);

        if (!w.good())
        {
            std::fprintf(stderr, "SaveState: write error: %s\n", path.c_str());
            return false;
        }

        return true;
    }

    bool SaveState::load(GameBoy& gb, const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            std::fprintf(stderr, "SaveState: failed to open for reading: %s\n", path.c_str());
            return false;
        }

        BinaryReader r(file);

        // Validate header
        char magic[4]{};
        r.readBlock(magic, 4);
        if (std::memcmp(magic, SAVE_STATE_MAGIC, 4) != 0)
        {
            std::fprintf(stderr, "SaveState: invalid magic in: %s\n", path.c_str());
            return false;
        }

        uint8_t version = r.read8();
        if (version != SAVE_STATE_VERSION)
        {
            std::fprintf(stderr, "SaveState: unsupported version %u in: %s\n", version, path.c_str());
            return false;
        }

        // Validate ROM hash
        uint32_t savedHash = r.read32();
        Cartridge* cart = gb.mmuMut().cartridge();
        uint32_t currentHash = cart ? romTitleHash(cart->romData(), cart->romSize()) : 0;
        if (savedHash != currentHash)
        {
            std::fprintf(stderr, "SaveState: ROM mismatch (hash 0x%08X vs 0x%08X) in: %s\n",
                         savedHash, currentHash, path.c_str());
            return false;
        }

        // CGB mode
        bool cgbMode = r.readBool();
        (void)cgbMode; // Already set by the loaded ROM

        // Deserialize all components
        gb.cpuMut().deserialize(r);
        gb.timerMut().deserialize(r);
        gb.mmuMut().deserialize(r);
        gb.ppuMut().deserialize(r);
        gb.apu().deserialize(r);
        gb.joypadMut().deserialize(r);

        if (cart)
            cart->deserialize(r);

        if (!r.good())
        {
            std::fprintf(stderr, "SaveState: read error or truncated file: %s\n", path.c_str());
            return false;
        }

        return true;
    }

    // ---- SaveFile (SRAM persistence) ----

    std::string SaveFile::getSavePath(const std::string& romPath)
    {
        size_t dot = romPath.rfind('.');
        if (dot == std::string::npos)
            return romPath + ".sav";
        return romPath.substr(0, dot) + ".sav";
    }

    bool SaveFile::hasBattery(const uint8_t* rom, size_t romSize)
    {
        if (romSize <= 0x0147)
            return false;
        uint8_t type = rom[0x0147];
        // Cartridge types with battery backup
        switch (type)
        {
            case 0x03: // MBC1+RAM+BATTERY
            case 0x06: // MBC2+BATTERY
            case 0x0F: // MBC3+TIMER+BATTERY
            case 0x10: // MBC3+TIMER+RAM+BATTERY
            case 0x13: // MBC3+RAM+BATTERY
            case 0x1B: // MBC5+RAM+BATTERY
            case 0x1E: // MBC5+RUMBLE+RAM+BATTERY
                return true;
            default:
                return false;
        }
    }

    bool SaveFile::save(const GameBoy& gb, const std::string& path)
    {
        const Cartridge* cart = gb.mmu().cartridge();
        if (!cart || cart->sramSize() == 0)
            return false;

        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            std::fprintf(stderr, "SaveFile: failed to open for writing: %s\n", path.c_str());
            return false;
        }

        file.write(reinterpret_cast<const char*>(cart->sram()),
                    static_cast<std::streamsize>(cart->sramSize()));

        return file.good();
    }

    bool SaveFile::load(GameBoy& gb, const std::string& path)
    {
        Cartridge* cart = gb.mmuMut().cartridge();
        if (!cart)
            return false;

        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return false; // Not an error — save file may not exist yet

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> data(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(data.data()), size))
        {
            std::fprintf(stderr, "SaveFile: failed to read: %s\n", path.c_str());
            return false;
        }

        cart->loadSRAM(data.data(), data.size());
        return true;
    }

}
