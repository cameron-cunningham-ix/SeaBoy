#pragma once

#include <cstdint>
#include <cstddef>

// SD card helper for SeaBoy Pico.
// Uses FatFs (via carlk3's no-OS-FatFS-SD-SPI-RPi-Pico library)
// on SPI1: SCK=GPIO14, MOSI=GPIO15, MISO=GPIO12, CS=GPIO13.

// Maximum number of ROM entries in the file list.
static constexpr unsigned int kMaxRomEntries = 32;

// Maximum filename length (8.3 = 12 + null, but allow longer FAT LFN).
static constexpr unsigned int kMaxFilenameLen = 64;

struct RomEntry
{
    char name[kMaxFilenameLen];
    uint32_t size;
};

class SDCard
{
public:
    // Mount the FAT32 filesystem. Returns true on success.
    bool mount();

    // Unmount the filesystem.
    void unmount();

    // List .gb and .gbc files in the root directory.
    // Populates entries[] and returns the count (up to kMaxRomEntries).
    unsigned int listROMs(RomEntry* entries, unsigned int maxEntries);

    // Read an entire file into a caller-provided buffer.
    // Returns bytes actually read, or 0 on failure.
    // path must be an absolute FatFs path (e.g., "0:/game.gb").
    uint32_t readFile(const char* path, uint8_t* buf, uint32_t maxSize);

private:
    bool m_mounted = false;
};
