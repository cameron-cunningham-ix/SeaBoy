#pragma once

#include "src/cartridge/Cartridge.hpp"

#include "ff.h"

#include <cstdio>
#include <cstring>

// SDCardBankLoader - streams 16 KB ROM banks from a FAT32 file on demand.
//
// Usage (in main_pico.cpp after GameBoy::loadROM with a 16 KB bank-0-only vector):
//
//   static SDCardBankLoader loader;
//   if (loader.open("0:/game.gb"))
//       s_gameBoy.mmuMut().cartridge()->setBankLoader(loader.makeCallback());
//
// The loader keeps the file open for the duration of emulation.
// Declare it static (or in BSS) — it must outlive the GameBoy instance.
//
// Thread safety: called from Core 0 only (MBC reads happen in the emulation loop).

class SDCardBankLoader
{
public:
    SDCardBankLoader() = default;
    ~SDCardBankLoader() { close(); }

    // Not copyable or movable (FatFs FIL is not safely movable).
    SDCardBankLoader(const SDCardBankLoader&)            = delete;
    SDCardBankLoader& operator=(const SDCardBankLoader&) = delete;

    // Open the ROM file. Returns true on success.
    // The file remains open until close() is called or the object is destroyed.
    bool open(const char* path)
    {
        close();
        FRESULT fr = f_open(&m_file, path, FA_READ);
        if (fr != FR_OK)
        {
            std::printf("SDCardBankLoader: f_open(%s) failed: %d\n", path, static_cast<int>(fr));
            return false;
        }
        m_open = true;
        std::printf("SDCardBankLoader: opened %s\n", path);
        return true;
    }

    void close()
    {
        if (m_open)
        {
            f_close(&m_file);
            m_open = false;
        }
    }

    bool isOpen() const { return m_open; }

    // Returns a BankLoader callback bound to this loader instance.
    // The returned function fills `buf` with 16 KB from offset (bankNum * 0x4000).
    SeaBoy::Cartridge::BankLoader makeCallback()
    {
        return [this](uint8_t* buf, uint16_t bankNum)
        {
            load(buf, bankNum);
        };
    }

private:
    FIL  m_file{};
    bool m_open = false;

    void load(uint8_t* buf, uint16_t bankNum)
    {
        if (!m_open)
        {
            std::memset(buf, 0xFF, 0x4000);
            return;
        }

        FSIZE_t offset = static_cast<FSIZE_t>(bankNum) * 0x4000u;
        FRESULT fr = f_lseek(&m_file, offset);
        if (fr != FR_OK)
        {
            std::printf("SDCardBankLoader: f_lseek bank %u failed: %d\n",
                        static_cast<unsigned>(bankNum), static_cast<int>(fr));
            std::memset(buf, 0xFF, 0x4000);
            return;
        }

        UINT totalRead = 0;
        while (totalRead < 0x4000)
        {
            UINT chunk = 0;
            fr = f_read(&m_file, buf + totalRead, 0x4000 - totalRead, &chunk);
            if (fr != FR_OK || chunk == 0)
                break;
            totalRead += chunk;
        }

        if (totalRead < 0x4000)
            std::memset(buf + totalRead, 0xFF, 0x4000 - totalRead); // pad EOF bank
    }
};
