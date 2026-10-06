/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <AP_HAL/RCOutput.h>
#include <AP_HAL/Util.h>
#include "AP_HAL_RTL8735B_Namespace.h"
#include "Semaphores.h"

namespace RTL8735B { class RCOutput; }

class RTL8735B::RCOutput : public AP_HAL::RCOutput
{
public:
    RCOutput();
    void init() override;
    void set_freq(uint32_t chmask, uint16_t freq_hz) override;
    uint16_t get_freq(uint8_t chan) override;
    void enable_ch(uint8_t chan) override;
    void disable_ch(uint8_t chan) override;
    void write(uint8_t chan, uint16_t pulse_us) override;
    uint16_t read(uint8_t chan) override;
    void read(uint16_t *period_us, uint8_t len) override;
    void cork() override;
    void push() override;
    void set_output_mode(uint32_t mask, enum output_mode mode) override;
    enum output_mode get_output_mode(uint32_t &mask) override;
    void set_default_rate(uint16_t rate_hz) override;
    bool force_safety_on(void) override;
    void force_safety_off(void) override;

    AP_HAL::Util::safety_state safety_switch_state() const;

private:
    static constexpr uint8_t CHANNELS = 4;
    void _stop_channel(uint8_t chan);
    void _brushed_hold_low(uint8_t chan);
    void _apply_channel(uint8_t chan);
    void _commit_write(uint8_t chan, uint16_t pulse_us);
    void _refresh_rate(uint8_t chan);
    void _report_brushed_rate(uint8_t chan);
    void _set_freq_locked(uint32_t chmask, uint16_t freq_hz, bool mark_fast);
    bool _channel_valid(uint8_t chan) const;
    bool _output_usable(uint8_t chan) const;

    uint16_t _freq_hz[CHANNELS];
    // 最後一次 set_freq 要求的頻率；切換輸出模式時依新模式重新判斷是否支援。
    uint16_t _requested_hz[CHANNELS];
    uint16_t _period_us[CHANNELS];
    // 有刷模式的週期（PWM SCLK tick）。
    uint16_t _period_ticks[CHANNELS];
    uint16_t _value_us[CHANNELS];
    uint16_t _pending_us[CHANNELS];
    enum output_mode _mode[CHANNELS];
    bool _enabled[CHANNELS];
    bool _has_valid_write[CHANNELS];
    bool _rate_valid[CHANNELS];
    uint32_t _pending_mask;
    // 以 set_freq 設為高於 50 Hz 或設為有刷模式的通道；set_default_rate 不改動它們。
    uint32_t _fast_mask;
    // 已回報「有刷頻率不支援、寫入被拒」的通道；該通道收到支援的頻率後清除，之後再失敗會再回報。
    uint32_t _brushed_rate_reported_mask;
    bool _corked;
    bool _initialized;
    AP_HAL::Util::safety_state _safety_state;
    mutable Semaphore _semaphore;
};
