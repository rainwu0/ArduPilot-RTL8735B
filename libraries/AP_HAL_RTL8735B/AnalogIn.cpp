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
    if (pin != ANALOG_INPUT_NONE && !_bind(pin)) {
        _pin = ANALOG_INPUT_NONE;
        _valid = false;
    }
}

// 原始取樣換成腳上的電壓：12 位元、滿刻度 3.3 V 的線性換算，偏移為 0。
// 依據：12 位元見 SDK d1b6426 的 rtl8735b_adc.h（HP_ADC_RESOLUTION）；滿刻度 3.3 V 與同一顆晶片的官方 Arduino 套件相同
// （Ameba-AIoT/ameba-arduino-pro2 93d6351 的 Arduino_package/hardware/cores/ambpro2/wiring_analog.c 以 3300 mV 為滿刻度）。
// 換算的精度與零點偏移未經板上量測；零點偏移由上層參數修正（例如 AP_BattMonitor_Analog 的 BATT_VLT_OFFSET）。
static constexpr float ADC_FULL_SCALE_VOLTS = 3.3f;
static constexpr float ADC_CODE_MAX = 4095.0f;

static float raw_to_volts(float raw)
{
    return raw * (ADC_FULL_SCALE_VOLTS / ADC_CODE_MAX);   // NaN 照樣傳下去
}

// 上層（電池、RSSI、空速、測距等類比 backend）以 set_pin() 的結果判斷可用與否，
// 例如 AP_BattMonitor_Analog 直接當作 healthy。只接受板型 hwdef 列出的類比腳。
bool AnalogIn::Source::set_pin(uint8_t pin)
{
    return _bind(pin);
}

// 目前 _timer_tick 只讀 hwdef 的第一條 RTL8735B_ADC，再餵給所有 Source；板型只列一條時沒有問題。
bool AnalogIn::Source::_bind(uint8_t pin)
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
    return raw_to_volts(read_average());
}

float AnalogIn::Source::voltage_latest()
{
    return raw_to_volts(read_latest());
}

// 沒有 5 V 電源的監測，ratiometric 與一般電壓相同（比照 AP_HAL_ESP32）
float AnalogIn::Source::voltage_average_ratiometric()
{
    return voltage_average();
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
    const int adc_status = rtl8735b_adc_init(rtl8735b_adc_logical_pin(0));
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

    const int32_t sample = rtl8735b_adc_read_u16(rtl8735b_adc_logical_pin(0));
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
