#pragma once

#include <cstdint>

#include "src/core/Joypad.hpp"   // SeaBoy::Button enum

namespace SeaBoy { class GameBoy; }

// GPIO button driver for the SeaBoy handheld.
// All buttons are active-low with internal pull-ups.
//
// GPIO map (physical Pico pin in parentheses):
//   GPIO 2  (pin 4)  -> Up
//   GPIO 3  (pin 5)  -> Down
//   GPIO 4  (pin 6)  -> Left
//   GPIO 5  (pin 7)  -> Right
//   GPIO 6  (pin 9)  -> A
//   GPIO 7  (pin 10) -> B
//   GPIO 8  (pin 11) -> Select
//   GPIO 9  (pin 12) -> Start
//   GPIO 10 (pin 14) -> Settings (in-game menu; not forwarded to GameBoy)

class Buttons
{
public:
    // Initialise all 9 GPIO pins as inputs with pull-ups.
    // Call once after pico_sdk_init() / stdio_init_all().
    void init();

    // Poll all GPIOs, detect edges, call gb.setButton() for GB buttons,
    // update settingsPressed() for the settings button, and fire the
    // optional change callback for each edge.
    void poll(SeaBoy::GameBoy& gb);

    // Optional debug / UI callback fired on every button edge.
    //   btn        - GB button value (undefined / ignore when isSettings == true)
    //   pressed    - true = just pressed, false = just released
    //   isSettings - true = settings button, not a GB button
    using ChangeCallback = void(*)(SeaBoy::Button btn, bool pressed, bool isSettings);
    void setChangeCallback(ChangeCallback cb) { m_changeCb = cb; }

    // True if the settings button has been pressed since the last clearSettings().
    bool settingsPressed() const { return m_settingsEdge; }
    void clearSettings()         { m_settingsEdge = false; }

private:
    static constexpr unsigned int kFirstGpio    = 2;
    static constexpr unsigned int kSettingsGpio = 10;
    static constexpr unsigned int kNumGBButtons = 8;   // GPIOs 2-9

    // Snapshot of GPIO 2-10 from last poll(), shifted down to bit 0.
    // Active-low: bit = 1 means released, bit = 0 means pressed.
    uint32_t       m_prevMask     = 0x1FFu;
    bool           m_settingsEdge = false;
    ChangeCallback m_changeCb     = nullptr;
};
