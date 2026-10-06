/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "AnalogIn.h"

#include <AP_HAL/AP_HAL.h>
#include <AP_HAL/AP_HAL_Macros.h>
#include <math.h>
#include <stdio.h>

#include "IOConfig.h"
#include "io_shim.h"

extern const AP_HAL::HAL &hal;

using namespace RTL8735B;

AnalogIn::Source::Source(uint8_t pin) :
    _pin(ANALOG_INPUT_NONE),
    _valid(false),
    _latest(NAN),
    _sum(0),
    _count(0),
    _has_sample(false)
{
    if (pin != ANALOG_INPUT_NONE && !set_pin(pin)) {
        _pin = ANALOG_INPUT_NONE;
        _valid = false;
    }
}

bool AnalogIn::Source::set_pin(uint8_t pin)
{
    if (rtl8735b_analog_pin(pin) == RTL8735B_INVALID_PIN) {
        return false;
    }
    _semaphore.take_blocking();
    _pin = pin;
    _valid = true;
    _latest = NAN;
    _sum = 0;
    _count = 0;
    _has_sample = false;
    _semaphore.give();
    return true;
}

float AnalogIn::Source::read_average()
{
    _semaphore.take_blocking();
    const float value = !_has_sample ? NAN :
        (_count == 0 ? _latest : (float)_sum / (float)_count);
    _sum = 0;
    _count = 0;
    _semaphore.give();
    return value;
}

float AnalogIn::Source::read_latest()
{
    _semaphore.take_blocking();
    const float value = _latest;
    _semaphore.give();
    return value;
}

float AnalogIn::Source::voltage_average()
{
    // 參考電壓尚未由 SDK 文件或目標板量測確認，不換算成伏特。
    return NAN;
}

float AnalogIn::Source::voltage_latest()
{
    return NAN;
}

float AnalogIn::Source::voltage_average_ratiometric()
{
    return NAN;
}

void AnalogIn::Source::add_sample(uint16_t sample)
{
    _semaphore.take_blocking();
    if (_valid) {
        _latest = (float)sample;
        _sum += sample;
        _count++;
        _has_sample = true;
        if (_count >= 254U) {
            _sum >>= 1;
            _count >>= 1;
        }
    }
    _semaphore.give();
}

bool AnalogIn::Source::active()
{
    _semaphore.take_blocking();
    const bool value = _valid;
    _semaphore.give();
    return value;
}

AnalogIn::AnalogIn() :
    _num_channels(0),
    _initialized(false),
    _adc_ready(false)
{}

void AnalogIn::init()
{
    if (_initialized) {
        return;
    }
    // SDK ADC 首次初始化會等待 20 ms，因此在 ArduPilot 啟動階段完成。
    const int adc_status = rtl8735b_adc_init(RTL8735B_ADC_LOGICAL0);
    _adc_ready = adc_status == 0;
    if (!_adc_ready) {
        printf("RTL8735B: ADC initialization failed (%d)\n", adc_status);
    }
    hal.scheduler->register_io_process(FUNCTOR_BIND_MEMBER(&AnalogIn::_timer_tick, void));
    _initialized = true;
}

AP_HAL::AnalogSource *AnalogIn::channel(int16_t pin)
{
    if (pin < 0 || pin > UINT8_MAX) {
        pin = ANALOG_INPUT_NONE;
    }
    if (!_adc_ready) {
        return nullptr;
    }
    if (pin != ANALOG_INPUT_NONE && !valid_analog_pin((uint16_t)pin)) {
        return nullptr;
    }

    _channels_semaphore.take_blocking();
    if (_num_channels >= MAX_CHANNELS) {
        _channels_semaphore.give();
        return nullptr;
    }
    Source *source = NEW_NOTHROW Source((uint8_t)pin);
    if (source != nullptr) {
        _channels[_num_channels++] = source;
    }
    _channels_semaphore.give();
    return source;
}

bool AnalogIn::valid_analog_pin(uint16_t pin) const
{
    return pin <= UINT8_MAX && rtl8735b_analog_pin((uint8_t)pin) != RTL8735B_INVALID_PIN;
}

float AnalogIn::board_voltage(void)
{
    return NAN;
}

void AnalogIn::_timer_tick()
{
    if (!_adc_ready) {
        return;
    }

    _channels_semaphore.take_blocking();
    bool active = false;
    for (uint8_t i = 0; i < _num_channels; i++) {
        active = active || (_channels[i] != nullptr && _channels[i]->active());
    }
    _channels_semaphore.give();
    if (!active) {
        return;
    }

    const int32_t sample = rtl8735b_adc_read_u16(RTL8735B_ADC_LOGICAL0);
    if (sample < 0 || sample > 4095) {
        return;
    }
    _channels_semaphore.take_blocking();
    for (uint8_t i = 0; i < _num_channels; i++) {
        if (_channels[i] != nullptr) {
            _channels[i]->add_sample((uint16_t)sample);
        }
    }
    _channels_semaphore.give();
}
