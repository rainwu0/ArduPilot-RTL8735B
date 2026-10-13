#pragma once
#include <stdint.h>
#include <string.h>
#include <functional>
#define AP_RCPROTOCOL_ENABLED 1
class AP_RCProtocol {
public:
    unsigned init_calls = 0;
    bool pending = false;
    uint8_t count = 0;
    uint16_t channels[32]{};
    int16_t rssi = -1;
    int16_t quality = -1;
    std::function<void()> before_read;
    void init() { init_calls++; }
    bool new_input() {
        if (before_read) { before_read(); }
        const bool p = pending;
        pending = false;
        return p;
    }
    uint8_t num_channels() { return count; }
    void read(uint16_t *out, uint8_t n) { memcpy(out, channels, n * sizeof(*out)); }
    int16_t get_RSSI() { return rssi; }
    int16_t get_rx_link_quality() { return quality; }
    const char *detected_protocol_name() { return "CRSF"; }
};
namespace AP { AP_RCProtocol &RC(); }
