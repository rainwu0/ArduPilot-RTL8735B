/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>
#include <AP_HAL/RCInput.h>
#include <AP_HAL_RTL8735B/Semaphores.h>

namespace RTL8735B {

// 解碼沿用 AP_RCProtocol；本類只把完整的有效 frame 發布給 AP_HAL 使用者。
class RCInput : public AP_HAL::RCInput {
public:
    void init() override;
    bool new_input() override;
    uint8_t num_channels() override;
    uint16_t read(uint8_t channel) override;
    uint8_t read(uint16_t *periods, uint8_t length) override;
    int16_t get_rssi() override;
    int16_t get_rx_link_quality() override;
    const char *protocol() const override;
    void _timer_tick();

private:
    static constexpr uint8_t MAX_CHANNELS = 18;
    uint16_t _channels[MAX_CHANNELS]{};
    uint8_t _count = 0;
    uint32_t _generation = 0;
    uint32_t _last_read = 0;
    int16_t _rssi = -1;
    int16_t _link_quality = -1;
    const char *_protocol = nullptr;
    bool _initialized = false;
    mutable Semaphore _mutex;
};

}
