/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <AP_HAL/AnalogIn.h>
#include "AP_HAL_RTL8735B_Namespace.h"
#include "Semaphores.h"

namespace RTL8735B { class AnalogIn; }

class RTL8735B::AnalogIn : public AP_HAL::AnalogIn
{
public:
    AnalogIn();
    void init() override;
    AP_HAL::AnalogSource *channel(int16_t pin) override;
    bool valid_analog_pin(uint16_t pin) const override;
    float board_voltage(void) override;

private:
    class Source : public AP_HAL::AnalogSource {
    public:
        explicit Source(uint8_t pin);
        float read_average() override;
        float read_latest() override;
        bool set_pin(uint8_t pin) override WARN_IF_UNUSED;
        float voltage_average() override;
        float voltage_latest() override;
        float voltage_average_ratiometric() override;

        bool active();
        void add_sample(uint16_t sample);

    private:
        uint8_t _pin;
        bool _valid;
        float _latest;
        uint32_t _sum;
        uint16_t _count;
        bool _has_sample;
        Semaphore _semaphore;
    };

    static constexpr uint8_t MAX_CHANNELS = 16;
    void _timer_tick();

    Source *_channels[MAX_CHANNELS]{};
    uint8_t _num_channels;
    bool _initialized;
    bool _adc_ready;
    Semaphore _channels_semaphore;
};
