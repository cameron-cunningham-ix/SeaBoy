#include "storage/SDCard.hpp"

#include "f_util.h"
#include "ff.h"
#include "hw_config.h"

#include <cstdio>
#include <cstring>

// FatFs static objects
static FATFS s_fs;

bool SDCard::mount()
{
    if (m_mounted)
        return true;

    FRESULT fr = f_mount(&s_fs, "0:", 1);
    if (fr != FR_OK)
    {
        printf("SD: f_mount failed: %s (%d)\n", FRESULT_str(fr), fr);
        return false;
    }

    m_mounted = true;
    printf("SD: mounted OK\n");
    return true;
}

void SDCard::unmount()
{
    if (m_mounted)
    {
        f_unmount("0:");
        m_mounted = false;
    }
}

// Check if a filename ends with .gb or .gbc (case-insensitive).
static bool isRomFile(const char* name)
{
    size_t len = std::strlen(name);
    // Check .gbc (4-char extension)
    if (len >= 4)
    {
        const char* ext = name + len - 4;
        if (ext[0] == '.' &&
            (ext[1] == 'g' || ext[1] == 'G') &&
            (ext[2] == 'b' || ext[2] == 'B') &&
            (ext[3] == 'c' || ext[3] == 'C'))
            return true;
    }
    // Check .gb (3-char extension)
    if (len >= 3)
    {
        const char* ext = name + len - 3;
        if (ext[0] == '.' &&
            (ext[1] == 'g' || ext[1] == 'G') &&
            (ext[2] == 'b' || ext[2] == 'B'))
            return true;
    }
    return false;
}

unsigned int SDCard::listROMs(RomEntry* entries, unsigned int maxEntries)
{
    DIR dir;
    FILINFO fno;
    unsigned int count = 0;

    FRESULT fr = f_opendir(&dir, "0:/");
    if (fr != FR_OK)
    {
        printf("SD: f_opendir failed: %s (%d)\n", FRESULT_str(fr), fr);
        return 0;
    }

    while (count < maxEntries)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0')
            break;

        // Skip directories and hidden/system files
        if (fno.fattrib & (AM_DIR | AM_HID | AM_SYS))
            continue;

        if (!isRomFile(fno.fname))
            continue;

        std::strncpy(entries[count].name, fno.fname, kMaxFilenameLen - 1);
        entries[count].name[kMaxFilenameLen - 1] = '\0';
        entries[count].size = static_cast<uint32_t>(fno.fsize);
        ++count;
    }

    f_closedir(&dir);
    printf("SD: found %u ROM(s)\n", count);
    return count;
}

uint32_t SDCard::readFile(const char* path, uint8_t* buf, uint32_t maxSize)
{
    FIL fil;
    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK)
    {
        printf("SD: f_open(%s) failed: %s (%d)\n", path, FRESULT_str(fr), fr);
        return 0;
    }

    UINT bytesRead = 0;
    fr = f_read(&fil, buf, maxSize, &bytesRead);
    f_close(&fil);

    if (fr != FR_OK)
    {
        printf("SD: f_read failed: %s (%d)\n", FRESULT_str(fr), fr);
        return 0;
    }

    printf("SD: read %u bytes from %s\n", bytesRead, path);
    return static_cast<uint32_t>(bytesRead);
}
