/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <AP_HAL/GPIO.h>
#include "AP_HAL_RTL8735B_Namespace.h"
#include "Semaphores.h"

namespace RTL8735B { class GPIO; }

class RTL8735B::GPIO : public AP_HAL::GPIO
{
public:
    GPIO();

    void init() override;
    void pinMode(uint8_t pin, uint8_t output) override;
    uint8_t read(uint8_t pin) override;
    void write(uint8_t pin, uint8_t value) override;
    void toggle(uint8_t pin) override;
    bool valid_pin(uint8_t pin) const override;
    AP_HAL::DigitalSource *channel(uint16_t pin) override;
    bool usb_connected(void) override;

    bool attach_interrupt(uint8_t pin, irq_handler_fn_t fn, INTERRUPT_TRIGGER_TYPE mode) override;
    bool attach_interrupt(uint8_t pin, AP_HAL::Proc proc, INTERRUPT_TRIGGER_TYPE mode) override;

private:
    static void _irq_trampoline(uint32_t irq_id, uint8_t value, uint32_t timestamp_us);
    class Source : public AP_HAL::DigitalSource {
    public:
        Source(GPIO &gpio, uint8_t pin) : _gpio(gpio), _pin(pin) {}
        void mode(uint8_t output) override;
        uint8_t read() override;
        void write(uint8_t value) override;
        void toggle() override;
    private:
        GPIO &_gpio;
        uint8_t _pin;
    };

    Source _sources[3];
    Semaphore _irq_semaphore;
};
