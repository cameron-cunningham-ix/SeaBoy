#include "input/Buttons.hpp"
#include "src/core/GameBoy.hpp"

#include "hardware/gpio.h"

void Buttons::init()
{
    for (unsigned int gpio = kFirstGpio; gpio <= kSettingsGpio; ++gpio)
    {
        gpio_init(gpio);
        gpio_set_dir(gpio, GPIO_IN);
        gpio_pull_up(gpio);
    }

    // Snapshot initial state so the first poll() doesn't report spurious edges.
    m_prevMask = (gpio_get_all() >> kFirstGpio) & 0x1FFu;
}

void Buttons::poll(SeaBoy::GameBoy& gb)
{
    // GPIO 2-9 -> GB buttons, GPIO 10 -> settings.
    // Shift down so bit 0 = GPIO 2, bit 8 = GPIO 10.
    const uint32_t curr    = (gpio_get_all() >> kFirstGpio) & 0x1FFu;
    const uint32_t changed = curr ^ m_prevMask;
    m_prevMask = curr;

    if (!changed) return;

    // ---- GB buttons: bits 0-7 (GPIOs 2-9) --------------------------------
    // Physical GPIO to Button mapping matches the PCB layout.
    static constexpr SeaBoy::Button kMap[kNumGBButtons] = {
        SeaBoy::Button::Up,     // GPIO 2
        SeaBoy::Button::Down,   // GPIO 3
        SeaBoy::Button::Left,   // GPIO 4
        SeaBoy::Button::Right,  // GPIO 5
        SeaBoy::Button::A,      // GPIO 6
        SeaBoy::Button::B,      // GPIO 7
        SeaBoy::Button::Select, // GPIO 8
        SeaBoy::Button::Start,  // GPIO 9
    };
    static_assert(sizeof(kMap) / sizeof(kMap[0]) == kNumGBButtons);

    for (unsigned int i = 0; i < kNumGBButtons; ++i)
    {
        if (!(changed & (1u << i))) continue;

        const bool pressed = !(curr & (1u << i)); // active-low: 0 = pressed
        gb.setButton(kMap[i], pressed);
        if (m_changeCb)
            m_changeCb(kMap[i], pressed, /*isSettings=*/false);
    }

    // ---- Settings button: bit 8 (GPIO 10) ---------------------------------
    if (changed & (1u << 8))
    {
        const bool pressed = !(curr & (1u << 8));
        if (pressed) m_settingsEdge = true;
        if (m_changeCb)
            m_changeCb(SeaBoy::Button::A, pressed, /*isSettings=*/true);
    }
}
