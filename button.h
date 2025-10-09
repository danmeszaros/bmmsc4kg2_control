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
    const static int DEBOUNCE_TIME_MS = 700;
    int pin;
    bool wasPressed;
    uint64_t lastInterruptTime;

    Button(int _pin) {
        pin = _pin;
        wasPressed = false;
        lastInterruptTime = 0;

        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        gpio_set_irq_enabled(pin,
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

        ButtonPriv::registerButton(pin, this);
    }

    bool pressed() {
        if (wasPressed) {
            wasPressed = false;
            return true;
        }

        return false;
    }

    void gpio_callback(uint gpio, uint32_t events) {
        uint64_t now = time_us_64() / 1000;

        if ((now - lastInterruptTime) < DEBOUNCE_TIME_MS) {
            return;  // Ignore bouncing
        }

        lastInterruptTime = now;

        if (events & GPIO_IRQ_EDGE_FALL) {
            wasPressed = true;
            //printf("Button %d pressed\n", i);
        } else if (events & GPIO_IRQ_EDGE_RISE) {
            // printf("Button %d released\n", i);
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
