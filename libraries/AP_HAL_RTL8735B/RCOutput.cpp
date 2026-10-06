/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "RCOutput.h"

#include <stdio.h>

#include "IOConfig.h"
#include "io_shim.h"

using namespace RTL8735B;

namespace {
class RCLock {
public:
    explicit RCLock(RTL8735B::Semaphore &semaphore) : _semaphore(semaphore)
    {
        _semaphore.take_blocking();
    }
    ~RCLock()
    {
        _semaphore.give();
    }
private:
    RTL8735B::Semaphore &_semaphore;
};
}

RCOutput::RCOutput() :
    _pending_mask(0),
    _fast_mask(0),
    _brushed_rate_reported_mask(0),
    _corked(false),
    _initialized(false),
    _safety_state(AP_HAL::Util::SAFETY_NONE)
{}

bool RCOutput::_channel_valid(uint8_t chan) const
{
    return chan < CHANNELS && rtl8735b_rcout_pin(chan) != RTL8735B_INVALID_PIN;
}

bool RCOutput::_output_usable(uint8_t chan) const
{
    return (_mode[chan] == MODE_PWM_NORMAL || _mode[chan] == MODE_PWM_BRUSHED) && _rate_valid[chan];
}

void RCOutput::init()
{
    RCLock lock(_semaphore);
    if (_initialized) {
        return;
    }
    for (uint8_t i = 0; i < CHANNELS; i++) {
        _freq_hz[i] = 50;
        _requested_hz[i] = 50;
        _period_us[i] = 20000;
        _period_ticks[i] = 0;
        _value_us[i] = 0;
        _pending_us[i] = 0;
        _mode[i] = MODE_PWM_NORMAL;
        _enabled[i] = false;
        _has_valid_write[i] = false;
        _rate_valid[i] = true;
    }
    _pending_mask = 0;
    _fast_mask = 0;
    _brushed_rate_reported_mask = 0;
    _corked = false;
    _safety_state = AP_HAL::Util::SAFETY_NONE;
    // 不在此呼叫 pwmout_init；SDK 會在該呼叫中啟動 20 ms PWM 通道。
    // 此時還不知道各通道是一般 PWM 還是有刷馬達，腳位維持上電預設狀態，直到設定輸出模式或第一次輸出。
    _initialized = true;
}

void RCOutput::_stop_channel(uint8_t chan)
{
    if (!_channel_valid(chan)) {
        return;
    }
    if (_mode[chan] == MODE_PWM_BRUSHED) {
        _brushed_hold_low(chan);
        return;
    }
    rtl8735b_pwm_stop(chan);
}

// 有刷馬達由高電位導通的低側開關驅動，停止狀態要主動輸出 duty 0，而不是停用通道。
void RCOutput::_brushed_hold_low(uint8_t chan)
{
    static uint32_t reported_hold_low_failures;
    const uint16_t ticks = _rate_valid[chan] ? _period_ticks[chan] :
        rtl8735b_rc_brushed_period_ticks(RTL8735B_RC_BRUSHED_MIN_HZ);
    if (rtl8735b_pwm_apply_ticks(chan, ticks, 0) != 0 &&
        (reported_hold_low_failures & (1UL << chan)) == 0) {
        reported_hold_low_failures |= 1UL << chan;
        printf("RTL8735B: brushed PWM ch%u could not be set to zero duty\n", (unsigned)chan + 1U);
    }
}

void RCOutput::_apply_channel(uint8_t chan)
{
    if (!_channel_valid(chan) || !_initialized) {
        return;
    }
    const RTL8735BRCChannelPolicy policy = {
        _value_us[chan],
        _period_us[chan],
        (uint8_t)_enabled[chan],
        (uint8_t)_has_valid_write[chan],
        (uint8_t)_output_usable(chan),
        (uint8_t)(_safety_state == AP_HAL::Util::SAFETY_DISARMED),
    };
    if (!rtl8735b_rc_can_emit(&policy)) {
        _stop_channel(chan);
        return;
    }
    if (_mode[chan] == MODE_PWM_BRUSHED) {
        // 以 set_esc_scaling 的範圍把脈寬換成 duty。
        const uint16_t duty = rtl8735b_rc_brushed_duty_ticks(_value_us[chan], _esc_pwm_min,
                                                               _esc_pwm_max, _period_ticks[chan]);
        if (duty == 0) {
            _stop_channel(chan);
            return;
        }
        if (rtl8735b_pwm_apply_ticks(chan, _period_ticks[chan], duty) != 0) {
            _has_valid_write[chan] = false;
            _value_us[chan] = 0;
            _stop_channel(chan);
        }
        return;
    }
    if (rtl8735b_pwm_apply(chan, _period_us[chan], _value_us[chan]) != 0) {
        _has_valid_write[chan] = false;
        _value_us[chan] = 0;
        _stop_channel(chan);
    }
}

// 依目前模式判斷 _requested_hz 是否支援：一般 PWM 50–490 Hz，有刷 10–32 kHz。
// 不支援時保留上一個合法頻率供 get_freq 回報，但通道停止輸出（fail closed）。
// 這裡不回報不支援的有刷頻率：Copter 開機時 AP_MotorsMatrix::init 先以建構時的速率
// （主迴圈頻率 400 Hz）呼叫 rc_set_freq，切到有刷模式後 init_rc_out 才套用 RC_SPEED，
// 這個過渡每次開機都有；改在 write() 真的拒絕寫入時回報（_report_brushed_rate）。
void RCOutput::_refresh_rate(uint8_t chan)
{
    const uint16_t freq_hz = _requested_hz[chan];
    const bool brushed = _mode[chan] == MODE_PWM_BRUSHED;
    const bool valid_rate = brushed ? rtl8735b_rc_brushed_rate_valid(freq_hz) :
        rtl8735b_rc_rate_valid(freq_hz);
    _pending_mask &= ~(1UL << chan);
    _pending_us[chan] = 0;
    _rate_valid[chan] = valid_rate;
    if (valid_rate) {
        _freq_hz[chan] = freq_hz;
        _brushed_rate_reported_mask &= ~(1UL << chan);
        if (brushed) {
            _period_ticks[chan] = rtl8735b_rc_brushed_period_ticks(freq_hz);
        } else {
            _period_us[chan] = 1000000UL / freq_hz;
        }
    }
    // 頻率或模式變更後，舊脈寬失效；收到新的合法寫入前不輸出。
    _has_valid_write[chan] = false;
    _value_us[chan] = 0;
    _stop_channel(chan);
}

// 有刷通道因頻率不支援而拒絕寫入時（例如 RC_SPEED 超出範圍），每個通道印一次實際要求的頻率。
void RCOutput::_report_brushed_rate(uint8_t chan)
{
    if (_mode[chan] != MODE_PWM_BRUSHED || _rate_valid[chan] ||
        (_brushed_rate_reported_mask & (1UL << chan)) != 0) {
        return;
    }
    _brushed_rate_reported_mask |= 1UL << chan;
    printf("RTL8735B: brushed PWM ch%u %u Hz unsupported (%u-%u Hz); output held at zero\n",
           (unsigned)chan + 1U, (unsigned)_requested_hz[chan],
           (unsigned)RTL8735B_RC_BRUSHED_MIN_HZ, (unsigned)RTL8735B_RC_BRUSHED_MAX_HZ);
}

void RCOutput::_set_freq_locked(uint32_t chmask, uint16_t freq_hz, bool mark_fast)
{
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        if ((chmask & (1UL << chan)) == 0) {
            continue;
        }
        _requested_hz[chan] = freq_hz;
        if (mark_fast && freq_hz > 50U) {
            _fast_mask |= 1UL << chan;
        }
        _refresh_rate(chan);
    }
}

void RCOutput::set_freq(uint32_t chmask, uint16_t freq_hz)
{
    RCLock lock(_semaphore);
    if (!_initialized) {
        return;
    }
    _set_freq_locked(chmask, freq_hz, true);
}

uint16_t RCOutput::get_freq(uint8_t chan)
{
    RCLock lock(_semaphore);
    return _channel_valid(chan) ? _freq_hz[chan] : 0;
}

void RCOutput::enable_ch(uint8_t chan)
{
    RCLock lock(_semaphore);
    if (!_initialized || !_channel_valid(chan)) {
        return;
    }
    _enabled[chan] = true;
    _apply_channel(chan);
}

void RCOutput::disable_ch(uint8_t chan)
{
    RCLock lock(_semaphore);
    if (!_channel_valid(chan)) {
        return;
    }
    _enabled[chan] = false;
    _has_valid_write[chan] = false;
    _value_us[chan] = 0;
    _pending_mask &= ~(1UL << chan);
    _pending_us[chan] = 0;
    _stop_channel(chan);
}

void RCOutput::_commit_write(uint8_t chan, uint16_t pulse_us)
{
    if (!_channel_valid(chan)) {
        return;
    }
    if (pulse_us == 0) {
        _value_us[chan] = 0;
        _has_valid_write[chan] = false;
        _stop_channel(chan);
        return;
    }
    if (_safety_state == AP_HAL::Util::SAFETY_DISARMED || !_output_usable(chan) ||
        (_mode[chan] == MODE_PWM_NORMAL && !rtl8735b_rc_pulse_valid(pulse_us, _period_us[chan]))) {
        _value_us[chan] = 0;
        _has_valid_write[chan] = false;
        _stop_channel(chan);
        return;
    }
    _value_us[chan] = pulse_us;
    _has_valid_write[chan] = true;
    _apply_channel(chan);
}

void RCOutput::write(uint8_t chan, uint16_t pulse_us)
{
    RCLock lock(_semaphore);
    if (!_initialized || !_channel_valid(chan)) {
        return;
    }
    if (_safety_state == AP_HAL::Util::SAFETY_DISARMED) {
        _pending_mask &= ~(1UL << chan);
        _pending_us[chan] = 0;
        _value_us[chan] = 0;
        _has_valid_write[chan] = false;
        _stop_channel(chan);
        return;
    }
    if (pulse_us != 0 && (!_output_usable(chan) ||
                          (_mode[chan] == MODE_PWM_NORMAL &&
                           !rtl8735b_rc_pulse_valid(pulse_us, _period_us[chan])))) {
        _report_brushed_rate(chan);
        _pending_mask &= ~(1UL << chan);
        _pending_us[chan] = 0;
        _value_us[chan] = 0;
        _has_valid_write[chan] = false;
        _stop_channel(chan);
        return;
    }
    if (_corked) {
        _pending_us[chan] = pulse_us;
        _pending_mask |= 1UL << chan;
        return;
    }
    _commit_write(chan, pulse_us);
}

uint16_t RCOutput::read(uint8_t chan)
{
    RCLock lock(_semaphore);
    return _channel_valid(chan) ? _value_us[chan] : 0;
}

void RCOutput::read(uint16_t *period_us, uint8_t len)
{
    RCLock lock(_semaphore);
    if (period_us == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < len; i++) {
        period_us[i] = _channel_valid(i) ? _value_us[i] : 0;
    }
}

void RCOutput::cork()
{
    RCLock lock(_semaphore);
    if (!_corked) {
        _pending_mask = 0;
        _corked = true;
    }
}

void RCOutput::push()
{
    RCLock lock(_semaphore);
    if (!_corked) {
        return;
    }
    const uint32_t pending = _pending_mask;
    _pending_mask = 0;
    _corked = false;
    if (_safety_state == AP_HAL::Util::SAFETY_DISARMED) {
        for (uint8_t chan = 0; chan < CHANNELS; chan++) {
            _pending_us[chan] = 0;
            _has_valid_write[chan] = false;
            _value_us[chan] = 0;
            _stop_channel(chan);
        }
        return;
    }
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        if ((pending & (1UL << chan)) != 0) {
            _commit_write(chan, _pending_us[chan]);
        }
        _pending_us[chan] = 0;
    }
}

void RCOutput::set_output_mode(uint32_t mask, enum output_mode mode)
{
    RCLock lock(_semaphore);
    static uint32_t reported_unsupported_modes;
    if (!_initialized) {
        return;
    }
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        if ((mask & (1UL << chan)) == 0) {
            continue;
        }
        if (mode == MODE_PWM_NORMAL || mode == MODE_PWM_BRUSHED) {
            if (mode == MODE_PWM_BRUSHED) {
                _fast_mask |= 1UL << chan;
            }
            if (_mode[chan] != mode) {
                _mode[chan] = mode;
                // 依新模式重新判斷頻率；進入有刷模式時立即以 duty 0 驅動腳位。
                _refresh_rate(chan);
            }
        } else {
            if (mode != MODE_PWM_NONE && (unsigned)mode < 32U &&
                (reported_unsupported_modes & (1UL << (unsigned)mode)) == 0) {
                reported_unsupported_modes |= 1UL << (unsigned)mode;
                printf("RTL8735B: unsupported RC output mode %u; output stopped\n", (unsigned)mode);
            }
            _mode[chan] = MODE_PWM_NONE;
            _has_valid_write[chan] = false;
            _value_us[chan] = 0;
            _pending_mask &= ~(1UL << chan);
            _pending_us[chan] = 0;
            _stop_channel(chan);
        }
    }
}

enum AP_HAL::RCOutput::output_mode RCOutput::get_output_mode(uint32_t &mask)
{
    RCLock lock(_semaphore);
    enum output_mode reported_mode = MODE_PWM_NONE;
    mask = 0;
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        if (_mode[chan] != MODE_PWM_NONE && _mode[chan] != MODE_PWM_NORMAL) {
            reported_mode = _mode[chan];
            mask |= 1UL << chan;
        }
    }
    return reported_mode;
}

// SRV_Channels::enable_aux_servos() 每秒呼叫一次。不改動高速與有刷通道；
// 頻率未變的通道也不重設，避免每秒中斷輸出一次。
void RCOutput::set_default_rate(uint16_t rate_hz)
{
    RCLock lock(_semaphore);
    if (!_initialized) {
        return;
    }
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        if ((_fast_mask & (1UL << chan)) != 0) {
            continue;
        }
        if (_rate_valid[chan] && _requested_hz[chan] == rate_hz && _freq_hz[chan] == rate_hz) {
            continue;
        }
        _set_freq_locked(1UL << chan, rate_hz, false);
    }
}

bool RCOutput::force_safety_on(void)
{
    RCLock lock(_semaphore);
    _safety_state = AP_HAL::Util::SAFETY_DISARMED;
    _pending_mask = 0;
    _corked = false;
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        _pending_us[chan] = 0;
        _has_valid_write[chan] = false;
        _value_us[chan] = 0;
        _stop_channel(chan);
    }
    return true;
}

void RCOutput::force_safety_off(void)
{
    RCLock lock(_semaphore);
    _safety_state = AP_HAL::Util::SAFETY_ARMED;
    _pending_mask = 0;
    _corked = false;
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        _pending_us[chan] = 0;
    }
    // 不重播 force_safety_on() 丟棄的命令。
}

AP_HAL::Util::safety_state RCOutput::safety_switch_state() const
{
    RCLock lock(_semaphore);
    return _safety_state;
}
