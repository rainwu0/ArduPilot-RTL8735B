/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "GPIO.h"

#include "IOConfig.h"
#include "io_shim.h"
#include "sdk_shim.h"

using namespace RTL8735B;

namespace {
GPIO *irq_owner;
AP_HAL::GPIO::irq_handler_fn_t irq_handlers[3];
AP_HAL::Proc irq_procs[3];
constexpr uint8_t gpio_logical_pins[3] = {7, 25, 26};

class GPIOIrqLock {
public:
    explicit GPIOIrqLock(RTL8735B::Semaphore &semaphore) : _semaphore(semaphore)
    {
        _semaphore.take_blocking();
    }
    ~GPIOIrqLock()
    {
        _semaphore.give();
    }
private:
    RTL8735B::Semaphore &_semaphore;
};
}

GPIO::GPIO() :
    _sources{Source(*this, 7), Source(*this, 25), Source(*this, 26)}
{
    irq_owner = this;
}

void GPIO::init()
{
    // 腳位採首次 pinMode 時設定，HAL 初始化本身不改變板上腳位狀態。
}

void GPIO::pinMode(uint8_t pin, uint8_t output)
{
    if (!valid_pin(pin) || (output != HAL_GPIO_INPUT && output != HAL_GPIO_OUTPUT)) {
        return;
    }
    rtl8735b_gpio_mode(pin, output == HAL_GPIO_OUTPUT);
}

uint8_t GPIO::read(uint8_t pin)
{
    if (!valid_pin(pin)) {
        return 0;
    }
    const int value = rtl8735b_gpio_read(pin);
    return value < 0 ? 0 : (uint8_t)value;
}

void GPIO::write(uint8_t pin, uint8_t value)
{
    if (valid_pin(pin)) {
        rtl8735b_gpio_write(pin, value != 0);
    }
}

void GPIO::toggle(uint8_t pin)
{
    if (valid_pin(pin)) {
        write(pin, read(pin) == 0);
    }
}

bool GPIO::valid_pin(uint8_t pin) const
{
    return rtl8735b_gpio_pin(pin) != RTL8735B_INVALID_PIN;
}

AP_HAL::DigitalSource *GPIO::channel(uint16_t pin)
{
    if (pin == 7) {
        return &_sources[0];
    }
    if (pin == 25) {
        return &_sources[1];
    }
    if (pin == 26) {
        return &_sources[2];
    }
    return nullptr;
}

bool GPIO::usb_connected(void)
{
    return false;
}

bool GPIO::attach_interrupt(uint8_t pin, irq_handler_fn_t fn, INTERRUPT_TRIGGER_TYPE mode)
{
    uint8_t trigger;
    if (mode == INTERRUPT_NONE || fn == nullptr) {
        if (!valid_pin(pin)) {
            return false;
        }
        GPIOIrqLock lock(_irq_semaphore);
        const uint8_t slot = pin == 7 ? 0 : (pin == 25 ? 1 : 2);
        rtl8735b_gpio_irq_detach(pin);
        const uint32_t irq_state = rtl8735b_irq_save_disable();
        irq_handlers[slot] = nullptr;
        irq_procs[slot] = nullptr;
        rtl8735b_irq_restore(irq_state);
        return true;
    }
    switch (mode) {
    case INTERRUPT_FALLING: trigger = 1; break;
    case INTERRUPT_RISING: trigger = 2; break;
    case INTERRUPT_BOTH: return false;
    default: return false;
    }
    if (!valid_pin(pin)) {
        return false;
    }
    GPIOIrqLock lock(_irq_semaphore);
    const uint8_t slot = pin == 7 ? 0 : (pin == 25 ? 1 : 2);
    rtl8735b_gpio_irq_detach(pin);
    pinMode(pin, HAL_GPIO_INPUT);
    const uint32_t irq_state = rtl8735b_irq_save_disable();
    irq_procs[slot] = nullptr;
    irq_handlers[slot] = fn;
    rtl8735b_irq_restore(irq_state);
    if (rtl8735b_gpio_irq_attach(pin, trigger, slot, _irq_trampoline) != 0) {
        const uint32_t restore_state = rtl8735b_irq_save_disable();
        irq_handlers[slot] = nullptr;
        rtl8735b_irq_restore(restore_state);
        return false;
    }
    return true;
}

bool GPIO::attach_interrupt(uint8_t pin, AP_HAL::Proc proc, INTERRUPT_TRIGGER_TYPE mode)
{
    uint8_t trigger;
    if (mode == INTERRUPT_NONE || proc == nullptr) {
        if (!valid_pin(pin)) {
            return false;
        }
        GPIOIrqLock lock(_irq_semaphore);
        const uint8_t slot = pin == 7 ? 0 : (pin == 25 ? 1 : 2);
        rtl8735b_gpio_irq_detach(pin);
        const uint32_t irq_state = rtl8735b_irq_save_disable();
        irq_handlers[slot] = nullptr;
        irq_procs[slot] = nullptr;
        rtl8735b_irq_restore(irq_state);
        return true;
    }
    switch (mode) {
    case INTERRUPT_FALLING: trigger = 1; break;
    case INTERRUPT_RISING: trigger = 2; break;
    case INTERRUPT_BOTH: return false;
    default: return false;
    }
    if (!valid_pin(pin)) {
        return false;
    }
    GPIOIrqLock lock(_irq_semaphore);
    const uint8_t slot = pin == 7 ? 0 : (pin == 25 ? 1 : 2);
    rtl8735b_gpio_irq_detach(pin);
    pinMode(pin, HAL_GPIO_INPUT);
    const uint32_t irq_state = rtl8735b_irq_save_disable();
    irq_handlers[slot] = nullptr;
    irq_procs[slot] = proc;
    rtl8735b_irq_restore(irq_state);
    if (rtl8735b_gpio_irq_attach(pin, trigger, slot, _irq_trampoline) != 0) {
        const uint32_t restore_state = rtl8735b_irq_save_disable();
        irq_procs[slot] = nullptr;
        rtl8735b_irq_restore(restore_state);
        return false;
    }
    return true;
}

void GPIO::_irq_trampoline(uint32_t irq_id, uint8_t value, uint32_t timestamp_us)
{
    if (irq_owner == nullptr || irq_id >= 3) {
        return;
    }
    const AP_HAL::GPIO::irq_handler_fn_t handler = irq_handlers[irq_id];
    const AP_HAL::Proc proc = irq_procs[irq_id];
    if (handler) {
        handler(gpio_logical_pins[irq_id], value != 0, timestamp_us);
    } else if (proc != nullptr) {
        proc();
    }
}

void GPIO::Source::mode(uint8_t output)
{
    _gpio.pinMode(_pin, output);
}

uint8_t GPIO::Source::read()
{
    return _gpio.read(_pin);
}

void GPIO::Source::write(uint8_t value)
{
    _gpio.write(_pin, value);
}

void GPIO::Source::toggle()
{
    _gpio.toggle(_pin);
}
