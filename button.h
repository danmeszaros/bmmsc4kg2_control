#pragma once
#include "pico/stdlib.h"

#include <map>

class ButtonReleased {
public:
    // pin: GPIO number
    // active_low: true if button pulls pin low when pressed
    // debounce_ms: ignore bounces shorter than this time
    // long_press_ms: press longer than this = long press
    ButtonReleased(uint pin, bool active_low = true, uint debounce_ms = 30, uint long_press_ms = 800)
        : pin_(pin), active_low_(active_low), debounce_ms_(debounce_ms), long_press_ms_(long_press_ms)
    {
        gpio_init(pin_);
        gpio_set_dir(pin_, GPIO_IN);
        if (active_low_)
            gpio_pull_up(pin_);
        else
            gpio_pull_down(pin_);

        // Register this instance by pin
        instances_[pin_] = this;

        gpio_set_irq_enabled_with_callback(
            pin_,
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
            true,
            &ButtonReleased::gpio_callback
        );

        last_event_time_ = to_ms_since_boot(get_absolute_time());
        pressed_ = false;
        released_flag_ = false;
        press_start_time_ = 0;
    }

    // Returns true once per release; fills duration_ms and isLongPress
    bool buttonRelease(bool &isLongPress) {
        if (released_flag_) {
            released_flag_ = false;
            isLongPress = (press_duration_ >= long_press_ms_);
            return true;
        }
        return false;
    }

private:
    bool read_raw() const {
        bool level = gpio_get(pin_);
        return active_low_ ? !level : level;
    }

    static void gpio_callback(uint gpio, uint32_t events) {
        auto it = instances_.find(gpio);
        if (it != instances_.end() && it->second) {
            it->second->handle_interrupt(events);
        }
    }

    void handle_interrupt(uint32_t events) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((now - last_event_time_) < debounce_ms_) return;  // debounce
        last_event_time_ = now;

        bool current = read_raw();

        if (current && !pressed_) {
            // Button just pressed
            pressed_ = true;
            press_start_time_ = now;
        } else if (!current && pressed_) {
            // Button just released
            pressed_ = false;
            press_duration_ = now - press_start_time_;
            released_flag_ = true;
        }
    }

    uint pin_;
    bool active_low_;
    uint debounce_ms_;
    uint long_press_ms_;

    volatile bool pressed_;
    volatile bool released_flag_;
    uint32_t press_start_time_;
    uint32_t press_duration_;
    uint32_t last_event_time_;

    // Static map of pin → instance (one shared callback)
    inline static std::map<uint, ButtonReleased*> instances_{};
};

class ButtonPressed {
public:
    // pin: GPIO number
    // active_low: true if button pulls pin low when pressed
    // debounce_ms: debounce interval
    ButtonPressed(uint pin, bool active_low = true, uint debounce_ms = 200)
        : pin_(pin), active_low_(active_low), debounce_ms_(debounce_ms)
    {
        gpio_init(pin_);
        gpio_set_dir(pin_, GPIO_IN);
        if (active_low_)
            gpio_pull_up(pin_);
        else
            gpio_pull_down(pin_);

        // Register this instance by pin
        instances_[pin_] = this;

        gpio_set_irq_enabled_with_callback(
            pin_,
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL,
            true,
            &ButtonPressed::gpio_callback
        );

        last_event_time_ = to_ms_since_boot(get_absolute_time());
        pressed_flag_ = false;
        pressed_state_ = read_raw();
    }

    // Returns true once per button press
    bool buttonPressed() {
        if (pressed_flag_) {
            pressed_flag_ = false;
            return true;
        }
        return false;
    }

private:
    bool read_raw() const {
        bool level = gpio_get(pin_);
        return active_low_ ? !level : level;
    }

    static void gpio_callback(uint gpio, uint32_t events) {
        auto it = instances_.find(gpio);
        if (it != instances_.end() && it->second) {
            it->second->handle_interrupt(events);
        }
    }

    void handle_interrupt(uint32_t events) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if ((now - last_event_time_) < debounce_ms_) return;  // debounce
        last_event_time_ = now;

        bool current = read_raw();
        if (current && !pressed_state_) {
            // Just pressed
            pressed_flag_ = true;
        }
        pressed_state_ = current;
    }

    uint pin_;
    bool active_low_;
    uint debounce_ms_;

    volatile bool pressed_flag_;
    bool pressed_state_;
    uint32_t last_event_time_;

    // Static map of pin → instance (one shared callback)
    inline static std::map<uint, ButtonPressed*> instances_{};
};
