/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
#include "RCInput.h"
#include <AP_RCProtocol/AP_RCProtocol.h>
#include <string.h>

void RTL8735B::RCInput::init()
{
    WITH_SEMAPHORE(_mutex);
    if (_initialized) {
        return;
    }
#if AP_RCPROTOCOL_ENABLED
    AP::RC().init();
#endif
    _initialized = true;
}

void RTL8735B::RCInput::_timer_tick()
{
    // 只由獨立 RC task 呼叫；new_input() 在 AP_RCProtocol 中保留既有失聯/失效 frame 語意。
    {
        WITH_SEMAPHORE(_mutex);
        if (!_initialized) {
            return;
        }
    }
#if AP_RCPROTOCOL_ENABLED
    AP_RCProtocol &decoder = AP::RC();
    if (!decoder.new_input()) {
        return;
    }
    const uint8_t reported = decoder.num_channels();
    if (reported == 0) {
        return;
    }
    const uint8_t count = reported < MAX_CHANNELS ? reported : MAX_CHANNELS;
    uint16_t channels[MAX_CHANNELS];
    decoder.read(channels, count);
    const int16_t rssi = decoder.get_RSSI();
    const int16_t quality = decoder.get_rx_link_quality();
    const char *protocol_name = decoder.detected_protocol_name();
    // 解碼器可能發出 GCS 通知；發布鎖只保護快照，不跨 UART/GCS 呼叫，以免鎖定順序反轉。
    WITH_SEMAPHORE(_mutex);
    _count = count;
    memcpy(_channels, channels, count * sizeof(*channels));
    _rssi = rssi;
    _link_quality = quality;
    _protocol = protocol_name;
    _generation++;
#endif
}

bool RTL8735B::RCInput::new_input()
{
    WITH_SEMAPHORE(_mutex);
    const bool updated = _initialized && _generation != _last_read;
    _last_read = _generation;
    return updated;
}

uint8_t RTL8735B::RCInput::num_channels()
{
    WITH_SEMAPHORE(_mutex);
    return _count;
}

uint16_t RTL8735B::RCInput::read(uint8_t channel)
{
    WITH_SEMAPHORE(_mutex);
    return channel < _count ? _channels[channel] : 0;
}

uint8_t RTL8735B::RCInput::read(uint16_t *periods, uint8_t length)
{
    WITH_SEMAPHORE(_mutex);
    if (periods == nullptr) {
        return 0;
    }
    const uint8_t count = length < _count ? length : _count;
    memcpy(periods, _channels, count * sizeof(*periods));
    return count;
}

int16_t RTL8735B::RCInput::get_rssi()
{
    WITH_SEMAPHORE(_mutex);
    return _rssi;
}

int16_t RTL8735B::RCInput::get_rx_link_quality()
{
    WITH_SEMAPHORE(_mutex);
    return _link_quality;
}

const char *RTL8735B::RCInput::protocol() const
{
    WITH_SEMAPHORE(_mutex);
    return _protocol;
}
#endif
