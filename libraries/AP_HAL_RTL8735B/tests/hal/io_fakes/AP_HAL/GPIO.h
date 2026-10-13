#pragma once
#include <stdint.h>

#define HAL_GPIO_INPUT 0
#define HAL_GPIO_OUTPUT 1
#define HAL_GPIO_ALT 2

namespace AP_HAL {
typedef void (*Proc)(void);
class DigitalSource {
public:
    virtual void mode(uint8_t output) = 0;
    virtual uint8_t read() = 0;
    virtual void write(uint8_t value) = 0;
    virtual void toggle() = 0;
};
class GPIO {
public:
    enum INTERRUPT_TRIGGER_TYPE { INTERRUPT_NONE, INTERRUPT_FALLING, INTERRUPT_RISING, INTERRUPT_BOTH };
    typedef void (*irq_handler_fn_t)(uint8_t pin, bool state, uint32_t timestamp_us);
    virtual void init() = 0;
    virtual void pinMode(uint8_t pin, uint8_t output) = 0;
    virtual uint8_t read(uint8_t pin) = 0;
    virtual void write(uint8_t pin, uint8_t value) = 0;
    virtual void toggle(uint8_t pin) = 0;
    virtual bool valid_pin(uint8_t pin) const = 0;
    virtual DigitalSource *channel(uint16_t pin) = 0;
    virtual bool usb_connected(void) = 0;
    virtual bool attach_interrupt(uint8_t pin, irq_handler_fn_t fn, INTERRUPT_TRIGGER_TYPE mode) = 0;
    virtual bool attach_interrupt(uint8_t pin, Proc proc, INTERRUPT_TRIGGER_TYPE mode) = 0;
};
}
