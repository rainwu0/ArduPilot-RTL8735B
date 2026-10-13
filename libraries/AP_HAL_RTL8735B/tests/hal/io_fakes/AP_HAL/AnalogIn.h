#pragma once
#include <stdint.h>
#include "AP_HAL_Macros.h"

#define ANALOG_INPUT_BOARD_VCC 254
#define ANALOG_INPUT_NONE 255

namespace AP_HAL {
class AnalogSource {
public:
    virtual float read_average() = 0;
    virtual float read_latest() = 0;
    virtual bool set_pin(uint8_t pin) WARN_IF_UNUSED = 0;
    virtual float voltage_average() = 0;
    virtual float voltage_latest() = 0;
    virtual float voltage_average_ratiometric() = 0;
};
class AnalogIn {
public:
    virtual void init() = 0;
    virtual AnalogSource *channel(int16_t pin) = 0;
    virtual bool valid_analog_pin(uint16_t pin) const = 0;
    virtual float board_voltage(void) = 0;
};
}
