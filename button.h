#pragma once
#include "pico/stdlib.h"

#include <map>
#include <algorithm>

class Button;

namespace ButtonPriv {
    std::map<int, Button*> buttons;
    bool initialized = false;

    void buttonPrivGpioCallback(uint gpio, uint32_t events);

    void registerButton(int pin, Button* button) {
        buttons[pin] = button;

        if (!initialized) {
            gpio_set_irq_callback(&buttonPrivGpioCallback);
            irq_set_enabled(IO_IRQ_BANK0, true);
            initialized = true;
        }
    }
};

class Button {
public:
    const static int DEBOUNCE_TIME_MS = 50;
    const static int LONG_PRESS = 500;
    int pin;

    bool eventPressed;
    bool eventReleased;
    bool eventLongPressed;
    

    uint64_t lastInterruptTime;

    uint64_t lastUpTime;
    uint64_t lastDownTime;
    uint64_t startDownTime;

    bool stableDown;

    Button(int _pin) {
        pin = _pin;
        eventPressed = false;
        eventReleased = false;
        eventLongPressed = false;

        lastInterruptTime = 0;
        lastUpTime = 0;
        lastDownTime = 0;
        startDownTime = 0;

        stableDown = false;

        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        gpio_set_irq_enabled(pin,
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

        ButtonPriv::registerButton(pin, this);
    }

    bool pressed() {
        if (eventPressed) {
            eventPressed = false;
            return true;
        }

        return false;
    }

    bool longPressed() {
        if (!eventLongPressed) {
            return false;
        }

        uint64_t now = time_us_64() / 1000;

        if (stableDown == true && (now - startDownTime) > LONG_PRESS) {
            eventLongPressed = false;
            return true;
        }

        return false;
    }

    bool released(bool& wasShort) {
        uint64_t now = time_us_64() / 1000;

        if (eventReleased && (now - lastUpTime) > DEBOUNCE_TIME_MS) {
            eventReleased = false;
            stableDown = false;

            wasShort = true;
            if ((now - startDownTime) > LONG_PRESS) {
                wasShort = false;
            }

            return true;
        }

        return false;
    }

    bool shortPressed() {
        bool wasShort;
        return released(wasShort) && wasShort;
    }

    void gpio_callback(uint gpio, uint32_t events) {
        uint64_t now = time_us_64() / 1000;

        if (events & GPIO_IRQ_EDGE_FALL) {
            lastDownTime = now;
            eventReleased = false;

            // released->pressed
            if (stableDown == false) {
                startDownTime = now;
                eventPressed = true;
                stableDown = true;
                eventLongPressed = true;
            }

        } else if (events & GPIO_IRQ_EDGE_RISE) {
            lastUpTime = now;
            eventReleased = true;
        }

    }
     
};

namespace ButtonPriv {
    void buttonPrivGpioCallback(uint gpio, uint32_t events) {
        auto it = buttons.find(gpio);

        if (it != buttons.end()) {
            ((Button*)(it->second))->gpio_callback(gpio, events);
        }
    }
};
