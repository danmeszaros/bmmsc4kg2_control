// Button.h
#ifndef BUTTON_H
#define BUTTON_H

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <vector>
#include <algorithm>

// Forward declarations
class ButtonBase;
class Button;
class ButtonRelease;

// Singleton manager for all buttons
class ButtonManager {
private:
    static ButtonManager* instance;
    static bool callbackRegistered;
    std::vector<ButtonBase*> buttons;
    
    ButtonManager() {}
    
    static void gpioCallback(uint gpio, uint32_t events);
    
public:
    static ButtonManager* getInstance() {
        if (!instance) {
            instance = new ButtonManager();
        }
        return instance;
    }
    
    void registerButton(ButtonBase* btn) {
        buttons.push_back(btn);
        
        if (!callbackRegistered) {
            gpio_set_irq_callback(&ButtonManager::gpioCallback);
            irq_set_enabled(IO_IRQ_BANK0, true);
            callbackRegistered = true;
        }
    }
    
    void unregisterButton(ButtonBase* btn) {
        auto it = std::find(buttons.begin(), buttons.end(), btn);
        if (it != buttons.end()) {
            buttons.erase(it);
        }
    }
};

ButtonManager* ButtonManager::instance = nullptr;
bool ButtonManager::callbackRegistered = false;


// Base class for all button types
class ButtonBase {
protected:
    uint pin;
    uint32_t debounceMs;
    volatile bool lastStableState;
    volatile uint32_t lastChangeTime;
    
    virtual void handleStateChange(bool newState, uint32_t now) = 0;

public:
    void checkInterrupt(uint gpio);
    
public:
    ButtonBase(uint gpioPin, uint32_t debounceMillis) 
        : pin(gpioPin), debounceMs(debounceMillis),
          lastStableState(false), lastChangeTime(0) {
        
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
        
        lastStableState = !gpio_get(pin);
        
        ButtonManager::getInstance()->registerButton(this);
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    }
    
    virtual ~ButtonBase() {
        gpio_set_irq_enabled(pin, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        ButtonManager::getInstance()->unregisterButton(this);
    }
    
    bool isPressed() const {
        return lastStableState;
    }
};

void ButtonBase::checkInterrupt(uint gpio) {
    if (gpio != pin) return;
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool rawState = !gpio_get(pin);  // Inverted: LOW = pressed
    
    if ((now - lastChangeTime) >= debounceMs) {
        if (rawState != lastStableState) {
            lastStableState = rawState;
            handleStateChange(rawState, now);
        }
        lastChangeTime = now;
    }
};

// Simple button - returns true once on press
class Button : public ButtonBase {
private:
    volatile bool pressedFlag;
    
    void handleStateChange(bool newState, uint32_t now) override {
        if (newState) {  // Button pressed
            pressedFlag = true;
        }
    }
    
public:
    Button(uint gpioPin, uint32_t debounceMillis = 50) 
        : ButtonBase(gpioPin, debounceMillis), pressedFlag(false) {}
    
    bool pressed() {
        if (pressedFlag) {
            pressedFlag = false;
            return true;
        }
        return false;
    }
};

// Button with release detection and long press
class ButtonRelease : public ButtonBase {
private:
    uint32_t longPressMs;
    volatile uint32_t pressStartTime;
    volatile bool releaseFlag;
    volatile bool wasShortPress;
    volatile bool longPressReported;
    
    void handleStateChange(bool newState, uint32_t now) override {
        if (newState) {  // Button pressed
            pressStartTime = now;
            longPressReported = false;
        } else {  // Button released
            uint32_t pressDuration = now - pressStartTime;
            releaseFlag = true;
            wasShortPress = (pressDuration < longPressMs);
        }
    }
    
public:
    ButtonRelease(uint gpioPin, uint32_t longPressMillis = 500, uint32_t debounceMillis = 50) 
        : ButtonBase(gpioPin, debounceMillis), longPressMs(longPressMillis),
          pressStartTime(0), releaseFlag(false), wasShortPress(false),
          longPressReported(false) {}
    
    bool released(bool& wasShort) {
        if (releaseFlag) {
            releaseFlag = false;
            wasShort = wasShortPress;
            return !longPressReported;
        }
        return false;
    }
    
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
};

void ButtonManager::gpioCallback(uint gpio, uint32_t events) {
    if (instance) {
        for (auto* btn : instance->buttons) {
            btn->checkInterrupt(gpio);
        }
    }
}

#endif // BUTTON_H

