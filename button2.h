// Button.h
#ifndef BUTTON_H
#define BUTTON_H

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <vector>
#include <algorithm>

class Button {
private:
    uint pin;
    uint32_t debounceMs;
    
    volatile bool lastStableState;
    volatile uint32_t lastChangeTime;
    volatile bool pressedFlag;
    
    static std::vector<Button*> instances;
    
    static void gpioCallback(uint gpio, uint32_t events) {
        for (auto* btn : instances) {
            if (btn->pin == gpio) {
                btn->handleInterrupt();
                break;
            }
        }
    }
    
    void handleInterrupt() {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        bool rawState = !gpio_get(pin);  // Inverted: LOW = pressed
        
        if ((now - lastChangeTime) >= debounceMs) {
            if (rawState != lastStableState) {
                lastStableState = rawState;
                if (rawState) {  // Button pressed
                    pressedFlag = true;
                }
            }
            lastChangeTime = now;
        }
    }
    
public:
    Button(uint gpioPin, uint32_t debounceMillis = 50) 
        : pin(gpioPin), debounceMs(debounceMillis),
          lastStableState(false), lastChangeTime(0), pressedFlag(false) {
        
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        
        lastStableState = !gpio_get(pin);
        instances.push_back(this);
        
        gpio_set_irq_enabled_with_callback(pin, 
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, 
            true, &Button::gpioCallback);
    }
    
    ~Button() {
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        auto it = std::find(instances.begin(), instances.end(), this);
        if (it != instances.end()) {
            instances.erase(it);
        }
    }
    
    bool pressed() {
        if (pressedFlag) {
            pressedFlag = false;
            return true;
        }
        return false;
    }
    
    bool isPressed() const {
        return lastStableState;
    }
};

std::vector<Button*> Button::instances;

class ButtonRelease {
private:
    uint pin;
    uint32_t debounceMs;
    uint32_t longPressMs;
    
    volatile bool lastStableState;
    volatile uint32_t lastChangeTime;
    volatile uint32_t pressStartTime;
    volatile bool releaseFlag;
    volatile bool wasShortPress;
    volatile bool longPressFlag;
    volatile bool longPressReported;
    
    static std::vector<ButtonRelease*> instances;
    
    static void gpioCallback(uint gpio, uint32_t events) {
        for (auto* btn : instances) {
            if (btn->pin == gpio) {
                btn->handleInterrupt();
                break;
            }
        }
    }
    
    void handleInterrupt() {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        bool rawState = !gpio_get(pin);  // Inverted: LOW = pressed
        
        if ((now - lastChangeTime) >= debounceMs) {
            if (rawState != lastStableState) {
                lastStableState = rawState;
                
                if (rawState) {  // Button pressed
                    pressStartTime = now;
                    longPressReported = false;
                } else {  // Button released
                    uint32_t pressDuration = now - pressStartTime;
                    releaseFlag = true;
                    wasShortPress = (pressDuration < longPressMs);
                }
            }
            lastChangeTime = now;
        }
    }
    
public:
    ButtonRelease(uint gpioPin, uint32_t longPressMillis = 500, uint32_t debounceMillis = 50) 
        : pin(gpioPin), debounceMs(debounceMillis), longPressMs(longPressMillis),
          lastStableState(false), lastChangeTime(0), pressStartTime(0),
          releaseFlag(false), wasShortPress(false), longPressFlag(false),
          longPressReported(false) {
        
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        
        lastStableState = !gpio_get(pin);
        instances.push_back(this);
        
        gpio_set_irq_enabled_with_callback(pin, 
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, 
            true, &ButtonRelease::gpioCallback);
    }
    
    ~ButtonRelease() {
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        auto it = std::find(instances.begin(), instances.end(), this);
        if (it != instances.end()) {
            instances.erase(it);
        }
    }
    
    // Returns true on release, parameter indicates if it was a short press
    // Returns false if long press was already reported
    bool released(bool& wasShort) {
        if (releaseFlag) {
            releaseFlag = false;
            wasShort = wasShortPress;
            return !longPressReported;  // Don't report release if long press was already handled
        }
        return false;
    }
    
    // Call this regularly to check for long press
    // Returns true once when long press threshold is reached (while still pressed)
    bool longPress() {
        if (lastStableState && !longPressReported) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if ((now - pressStartTime) >= longPressMs) {
                longPressReported = true;
                return true;
            }
        }
        return false;
    }
    
    bool isPressed() const {
        return lastStableState;
    }
};

std::vector<ButtonRelease*> ButtonRelease::instances;


#endif // BUTTON_H
