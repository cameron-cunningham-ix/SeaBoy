#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#ifndef PICO_BUILD
#include <fstream>
#include <vector>
#endif

// Save state serialization for SeaBoy emulator.
// Binary format: "SBST" magic + version byte + ROM title hash + sequential component data.

namespace SeaBoy
{
    class GameBoy;

#ifdef PICO_BUILD
    // On Pico, BinaryWriter/BinaryReader are no-op stubs.
    // serialize/deserialize methods still compile but do nothing.
    // Actual save-state I/O is handled via FatFs in later stages.
    struct BinaryWriter
    {
        void write8(uint8_t)             {}
        void write16(uint16_t)           {}
        void write32(uint32_t)           {}
        void writeBool(bool)             {}
        void writeInt(int)               {}
        void writeDouble(double)         {}
        void writeBlock(const void*, size_t) {}
        bool good() const { return false; }
    };

    struct BinaryReader
    {
        uint8_t  read8()                  { return 0; }
        uint16_t read16()                 { return 0; }
        uint32_t read32()                 { return 0; }
        bool     readBool()               { return false; }
        int      readInt()                { return 0; }
        double   readDouble()             { return 0.0; }
        void     readBlock(void*, size_t) {}
        bool     good() const             { return false; }
    };
#else
    // Simple binary writer wrapping std::ofstream.
    struct BinaryWriter
    {
        std::ofstream& out;

        explicit BinaryWriter(std::ofstream& s) : out(s) {}

        void write8(uint8_t v)   { out.write(reinterpret_cast<const char*>(&v), 1); }
        void write16(uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); }
        void write32(uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); }
        void writeBool(bool v)   { uint8_t b = v ? 1 : 0; write8(b); }
        void writeInt(int v)     { out.write(reinterpret_cast<const char*>(&v), sizeof(int)); }
        void writeDouble(double v) { out.write(reinterpret_cast<const char*>(&v), sizeof(double)); }
        void writeBlock(const void* data, size_t size)
        {
            out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        }

        bool good() const { return out.good(); }
    };

    // Simple binary reader wrapping std::ifstream.
    struct BinaryReader
    {
        std::ifstream& in;

        explicit BinaryReader(std::ifstream& s) : in(s) {}

        uint8_t  read8()   { uint8_t v = 0;  in.read(reinterpret_cast<char*>(&v), 1); return v; }
        uint16_t read16()  { uint16_t v = 0; in.read(reinterpret_cast<char*>(&v), 2); return v; }
        uint32_t read32()  { uint32_t v = 0; in.read(reinterpret_cast<char*>(&v), 4); return v; }
        bool     readBool(){ return read8() != 0; }
        int      readInt() { int v = 0; in.read(reinterpret_cast<char*>(&v), sizeof(int)); return v; }
        double   readDouble() { double v = 0; in.read(reinterpret_cast<char*>(&v), sizeof(double)); return v; }
        void readBlock(void* data, size_t size)
        {
            in.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
        }

        bool good() const { return in.good(); }
    };
#endif

    // Save state file format constants
    constexpr char SAVE_STATE_MAGIC[4] = {'S', 'B', 'S', 'T'};
    constexpr uint8_t SAVE_STATE_VERSION = 1;

    class SaveState
    {
    public:
        // Save full emulator state to file. Returns true on success.
        static bool save(const GameBoy& gb, const std::string& path);

        // Load full emulator state from file. Returns true on success.
        // The correct ROM must already be loaded in the GameBoy instance.
        static bool load(GameBoy& gb, const std::string& path);

        // Compute a 32-bit hash of the ROM title (header bytes 0x0134-0x0143)
        // for verifying save state matches the loaded ROM.
        static uint32_t romTitleHash(const uint8_t* rom, size_t romSize);
    };

    // Save file (battery-backed SRAM) utilities.
    class SaveFile
    {
    public:
        // Save SRAM to .sav file. Returns true on success.
        static bool save(const GameBoy& gb, const std::string& path);

        // Load SRAM from .sav file. Returns true on success.
        static bool load(GameBoy& gb, const std::string& path);

        // Derive .sav path from ROM path (replace extension).
        static std::string getSavePath(const std::string& romPath);

        // Check if the loaded cartridge has a battery (supports save files).
        static bool hasBattery(const uint8_t* rom, size_t romSize);
    };

}
