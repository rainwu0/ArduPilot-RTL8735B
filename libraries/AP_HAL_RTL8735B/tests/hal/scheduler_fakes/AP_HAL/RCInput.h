#pragma once

#include <stdint.h>

namespace AP_HAL {

class RCInput {
public:
    virtual ~RCInput() {}
    virtual void init() = 0;
    virtual bool new_input() = 0;
    virtual uint8_t num_channels() = 0;
    virtual uint16_t read(uint8_t channel) = 0;
    virtual uint8_t read(uint16_t *periods, uint8_t length) = 0;
    virtual int16_t get_rssi() = 0;
    virtual int16_t get_rx_link_quality() = 0;
    virtual const char *protocol() const = 0;
};

} // namespace AP_HAL
