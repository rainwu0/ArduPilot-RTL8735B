/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "RCOutput.h"

#include <AP_HAL/AP_HAL.h>

#include "IOConfig.h"
#include "StatusText.h"
#include "app/rtl8735b_app.h"
#include "dshot_shim.h"
#include "io_shim.h"

extern const AP_HAL::HAL &hal;

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
    _rate_reported_mask(0),
    _clock_reported_mask(0),
    _corked(false),
    _initialized(false),
    _safety_state(AP_HAL::Util::SAFETY_NONE),
    _dshot_mask(0),
    _dshot_attached_mask(0),
    _dshot_reported_mask(0),
    _telem_request_mask(0),
    _dshot_mode(MODE_PWM_NONE),
    _dshot_esc_type(DSHOT_ESC_NONE),
    _dshot_timing{},
    _dshot_timing_valid(false),
    _pwm_stalled(false),
    _dshot_sent(false),
    _dshot_stopped(false),
    _dshot_dropped_reported(false),
    _dshot_last_send_us(0),
    _dshot_commands{},
    _dshot_command_head(0),
    _dshot_command_count(0)
{
    for (uint8_t i = 0; i < CHANNELS; i++) {
        _dshot_slot[i] = -1;
    }
}

bool RCOutput::_channel_valid(uint8_t chan) const
{
    return chan < CHANNELS && rtl8735b_rcout_pin(chan) != RTL8735B_INVALID_PIN;
}

namespace {
// 走微秒路徑（G-Timer tick）的模式。OneShot 以連續 PWM 近似：硬體沒有單發觸發。
bool is_us_mode(AP_HAL::RCOutput::output_mode mode)
{
    return mode == AP_HAL::RCOutput::MODE_PWM_NORMAL || mode == AP_HAL::RCOutput::MODE_PWM_ONESHOT;
}

bool is_supported_mode(AP_HAL::RCOutput::output_mode mode)
{
    return is_us_mode(mode) || mode == AP_HAL::RCOutput::MODE_PWM_ONESHOT125 ||
        mode == AP_HAL::RCOutput::MODE_PWM_BRUSHED;
}

// DShot1200 不做：一位元只有約 417 個 CPU 週期
bool is_dshot_mode(AP_HAL::RCOutput::output_mode mode)
{
    return mode == AP_HAL::RCOutput::MODE_PWM_DSHOT150 || mode == AP_HAL::RCOutput::MODE_PWM_DSHOT300 ||
        mode == AP_HAL::RCOutput::MODE_PWM_DSHOT600;
}

uint16_t dshot_rate_khz(AP_HAL::RCOutput::output_mode mode)
{
    switch (mode) {
    case AP_HAL::RCOutput::MODE_PWM_DSHOT150:
        return 150;
    case AP_HAL::RCOutput::MODE_PWM_DSHOT300:
        return 300;
    case AP_HAL::RCOutput::MODE_PWM_DSHOT600:
        return 600;
    default:
        return 0;
    }
}

// 主程式每次 push 送一幀；主迴圈太久沒 push 時，計時器在上一幀之後這麼久補送同樣的幀，
// 讓電調不因收不到訊號而停機（最低約 250 Hz）
constexpr uint32_t DSHOT_KEEPALIVE_US = 4000;
// 主迴圈已經超過這麼久沒跑完一圈（rtl8735b_ardupilot_ms_since_main_loop）就當成停住：DShot 補送的油門幀改送 0，
// 解鎖中的 PWM 全部停止，馬達不會照停住前的油門一直轉。400 Hz 的主迴圈每 2.5 ms 跑完一圈，100 ms 是 40 圈
constexpr uint32_t MAIN_LOOP_STALE_MS = 100;
// 有命令在排隊時，計時器補送的間隔
constexpr uint32_t DSHOT_COMMAND_INTERVAL_US = 1000;
}

bool RCOutput::_is_dshot(uint8_t chan) const
{
    return (_dshot_mask & (1UL << chan)) != 0;
}

bool RCOutput::_output_usable(uint8_t chan) const
{
    return is_supported_mode(_mode[chan]) && _rate_valid[chan];
}

bool RCOutput::_pulse_valid(uint8_t chan, uint16_t pulse_us) const
{
    if (is_us_mode(_mode[chan])) {
        return rtl8735b_rc_pulse_valid(pulse_us, _period_us[chan]);
    }
    if (_mode[chan] == MODE_PWM_ONESHOT125) {
        return rtl8735b_rc_oneshot125_duty_ticks(pulse_us) != 0U;
    }
    return true;
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
    _rate_reported_mask = 0;
    _clock_reported_mask = 0;
    _corked = false;
    _safety_state = AP_HAL::Util::SAFETY_NONE;
    // 不在此呼叫 pwmout_init；SDK 會在該呼叫中啟動 20 ms PWM 通道。
    // 此時還不知道各通道是一般 PWM 還是有刷馬達，腳位維持上電預設狀態，直到設定輸出模式或第一次輸出。
    _initialized = true;
    // 主迴圈停住時停 PWM、DShot 的補送（_timer_tick）
    hal.scheduler->register_timer_process(FUNCTOR_BIND_MEMBER(&RCOutput::_timer_tick, void));
}

void RCOutput::_stop_channel(uint8_t chan)
{
    if (!_channel_valid(chan)) {
        return;
    }
    // 腳已改成 GPIO：兩幀之間本來就是低電位，脈寬清成 0 之後就不再送這一路
    if ((_dshot_attached_mask & (1UL << chan)) != 0) {
        return;
    }
    if (_mode[chan] == MODE_PWM_BRUSHED && _brushed_hold_low(chan)) {
        return;
    }
    // 有刷歸零失敗（例如 PWM 時脈仍由 OneShot125 占用）時，依硬體目前的狀態停止輸出，
    // 不能讓舊的輸出留著。
    rtl8735b_pwm_stop(chan);
}

// 有刷馬達由高電位導通的低側開關驅動，停止狀態要主動輸出 duty 0，而不是停用通道。
// 回傳是否已以 duty 0 驅動。
bool RCOutput::_brushed_hold_low(uint8_t chan)
{
    static uint32_t reported_hold_low_failures;
    const uint16_t ticks = _rate_valid[chan] ? _period_ticks[chan] :
        rtl8735b_rc_brushed_period_ticks(RTL8735B_RC_BRUSHED_MIN_HZ);
    const int status = rtl8735b_pwm_apply_ticks(chan, ticks, 0);
    if (status == RTL8735B_PWM_ERR_CLOCK) {
        _report_clock_conflict(chan);
    } else if (status != 0 && (reported_hold_low_failures & (1UL << chan)) == 0) {
        reported_hold_low_failures |= 1UL << chan;
        RTL8735B::report_warning("RTL8735B: brushed PWM ch%u could not be set to zero duty", (unsigned)chan + 1U);
    }
    return status == 0;
}

// 有刷與 OneShot125 共用 PWM SCLK：另一種模式已占用時，每個通道印一次原因。
void RCOutput::_report_clock_conflict(uint8_t chan)
{
    if ((_clock_reported_mask & (1UL << chan)) != 0) {
        return;
    }
    _clock_reported_mask |= 1UL << chan;
    if (_mode[chan] == MODE_PWM_BRUSHED) {
        RTL8735B::report_warning("RTL8735B: brushed PWM ch%u unavailable (PWM clock in use by OneShot125 output); "
                                 "output stopped", (unsigned)chan + 1U);
    } else {
        RTL8735B::report_warning("RTL8735B: OneShot125 ch%u unavailable (PWM clock in use by brushed output); "
                                 "output stopped", (unsigned)chan + 1U);
    }
}

void RCOutput::_apply_channel(uint8_t chan)
{
    if (!_channel_valid(chan) || !_initialized || (_dshot_attached_mask & (1UL << chan)) != 0) {
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
    if (_mode[chan] == MODE_PWM_ONESHOT125) {
        const int status = rtl8735b_pwm_apply_oneshot125(chan,
            rtl8735b_rc_oneshot125_duty_ticks(_value_us[chan]));
        if (status != 0) {
            if (status == RTL8735B_PWM_ERR_CLOCK) {
                _report_clock_conflict(chan);
            }
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

// 每個通道照目前記住的值重新輸出；不能輸出的照 _apply_channel 停止
void RCOutput::_apply_all_channels()
{
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        _apply_channel(chan);
    }
}

// 依目前模式判斷 _requested_hz 是否支援：一般 PWM 與 OneShot 50–490 Hz，有刷 10–32 kHz；
// OneShot125 的頻率固定（RTL8735B_ONESHOT125_HZ），不看要求的頻率。
// 不支援時保留上一個合法頻率供 get_freq 回報，但通道停止輸出（fail closed）。
// 這裡不回報不支援的有刷頻率：Copter 開機時 AP_MotorsMatrix::init 先以建構時的速率
// （主迴圈頻率 400 Hz）呼叫 rc_set_freq，切到有刷模式後 init_rc_out 才套用 RC_SPEED，
// 這個過渡每次開機都有；改在 write() 真的拒絕寫入時回報（_report_rate）。
void RCOutput::_refresh_rate(uint8_t chan)
{
    const uint16_t freq_hz = _requested_hz[chan];
    const bool brushed = _mode[chan] == MODE_PWM_BRUSHED;
    const bool oneshot125 = _mode[chan] == MODE_PWM_ONESHOT125;
    const bool valid_rate = oneshot125 || (brushed ? rtl8735b_rc_brushed_rate_valid(freq_hz) :
        rtl8735b_rc_rate_valid(freq_hz));
    _pending_mask &= ~(1UL << chan);
    _pending_us[chan] = 0;
    _rate_valid[chan] = valid_rate;
    if (oneshot125) {
        _freq_hz[chan] = RTL8735B_ONESHOT125_HZ;
    } else if (valid_rate) {
        _freq_hz[chan] = freq_hz;
        _rate_reported_mask &= ~(1UL << chan);
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

// 因頻率不支援而拒絕寫入時（例如 RC_SPEED 超出範圍），每個通道印一次實際要求的頻率。
void RCOutput::_report_rate(uint8_t chan)
{
    if (_rate_valid[chan] || (_rate_reported_mask & (1UL << chan)) != 0) {
        return;
    }
    if (_mode[chan] == MODE_PWM_BRUSHED) {
        RTL8735B::report_warning("RTL8735B: brushed PWM ch%u %u Hz unsupported (%u-%u Hz); output held at zero",
                                 (unsigned)chan + 1U, (unsigned)_requested_hz[chan],
                                 (unsigned)RTL8735B_RC_BRUSHED_MIN_HZ, (unsigned)RTL8735B_RC_BRUSHED_MAX_HZ);
    } else if (is_us_mode(_mode[chan])) {
        RTL8735B::report_warning("RTL8735B: PWM ch%u %u Hz unsupported (%u-%u Hz); output stopped",
                                 (unsigned)chan + 1U, (unsigned)_requested_hz[chan],
                                 (unsigned)RTL8735B_RC_MIN_HZ, (unsigned)RTL8735B_RC_MAX_HZ);
    } else {
        return;
    }
    _rate_reported_mask |= 1UL << chan;
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
        !_pulse_valid(chan, pulse_us)) {
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
    if (_is_dshot(chan)) {
        _pending_mask &= ~(1UL << chan);
        _pending_us[chan] = 0;
        if (_safety_state == AP_HAL::Util::SAFETY_DISARMED) {
            _value_us[chan] = 0;
            return;
        }
        if (_corked) {
            _pending_us[chan] = pulse_us;
            _pending_mask |= 1UL << chan;
            return;
        }
        _value_us[chan] = pulse_us;
        _dshot_send_locked();
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
    if (pulse_us != 0 && (!_output_usable(chan) || !_pulse_valid(chan, pulse_us))) {
        _report_rate(chan);
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
    // 主迴圈停住時計時器停掉了 PWM（_timer_tick）：主迴圈恢復後的第一次 push 把所有通道照最後的值恢復
    const bool resume = _pwm_stalled;
    _pwm_stalled = false;
    if (!_corked) {
        if (resume) {
            _apply_all_channels();
        }
        // 不經 cork 的 write() 也可能被延後，這裡同樣補寫。
        rtl8735b_pwm_flush();
        _dshot_send_locked();
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
            if (_is_dshot(chan)) {
                _value_us[chan] = _pending_us[chan];
            } else {
                _commit_write(chan, _pending_us[chan]);
            }
        }
        _pending_us[chan] = 0;
    }
    if (resume) {
        _apply_all_channels();
    }
    // 補寫上一次 push 時硬體還沒套用、因而延後的一般 PWM 值。
    rtl8735b_pwm_flush();
    // DShot 的各路同時送一幀
    _dshot_send_locked();
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
        if (is_dshot_mode(mode)) {
            _set_dshot_mode(chan, mode);
            continue;
        }
        if ((_dshot_attached_mask & (1UL << chan)) != 0) {
            // 腳已改成 GPIO，本次開機內不還給 PWM（改 MOT_PWM_TYPE 本來就要重開機）：停送並說明
            if ((_dshot_reported_mask & (1UL << chan)) == 0) {
                _dshot_reported_mask |= 1UL << chan;
                RTL8735B::report_warning("RTL8735B: ch%u stays a DShot pin until reboot; output stopped",
                                         (unsigned)chan + 1U);
            }
            _dshot_mask &= ~(1UL << chan);
            _mode[chan] = MODE_PWM_NONE;
            _has_valid_write[chan] = false;
            _value_us[chan] = 0;
            _pending_mask &= ~(1UL << chan);
            _pending_us[chan] = 0;
            continue;
        }
        if (is_supported_mode(mode)) {
            if (mode == MODE_PWM_BRUSHED || mode == MODE_PWM_ONESHOT125) {
                _fast_mask |= 1UL << chan;
            }
            if (_mode[chan] != mode) {
                // 先依舊模式停止輸出，再改模式：新模式可能因 PWM 時脈衝突而無法驅動腳位，不能讓舊的
                // 輸出留著。舊模式是有刷時先以 duty 0 驅動（停止狀態），失敗才停用通道。
                _stop_channel(chan);
                _mode[chan] = mode;
                // 依新模式重新判斷頻率；進入有刷模式時立即以 duty 0 驅動腳位。
                _refresh_rate(chan);
            }
        } else {
            if (mode != MODE_PWM_NONE && (unsigned)mode < 32U &&
                (reported_unsupported_modes & (1UL << (unsigned)mode)) == 0) {
                reported_unsupported_modes |= 1UL << (unsigned)mode;
                RTL8735B::report_warning("RTL8735B: unsupported RC output mode %u; output stopped", (unsigned)mode);
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

void RCOutput::_report_dshot_failure(uint8_t chan, int status)
{
    if ((_dshot_reported_mask & (1UL << chan)) != 0) {
        return;
    }
    _dshot_reported_mask |= 1UL << chan;
    const unsigned ch = (unsigned)chan + 1U;
    switch (status) {
    case RTL8735B_DSHOT_ECLOCK:
        RTL8735B::report_warning("RTL8735B: DShot ch%u unavailable: CPU cycle counter missing or not at the "
                                 "expected clock; output stopped", ch);
        break;
    case RTL8735B_DSHOT_EPORT:
        RTL8735B::report_warning("RTL8735B: DShot ch%u unavailable: pin not on the same GPIO port as the other "
                                 "DShot outputs; output stopped", ch);
        break;
    case RTL8735B_DSHOT_EGPIO:
        RTL8735B::report_warning("RTL8735B: DShot ch%u unavailable: pin cannot be used as GPIO (already in use?); "
                                 "output stopped", ch);
        break;
    default:
        RTL8735B::report_warning("RTL8735B: DShot ch%u unavailable (%d); output stopped", ch, status);
        break;
    }
}

void RCOutput::_update_dshot_timing()
{
    const uint8_t wide = _dshot_esc_type == DSHOT_ESC_BLHELI_S || _dshot_esc_type == DSHOT_ESC_BLHELI_EDT_S;
    _dshot_timing_valid = rtl8735b_dshot_timing(dshot_rate_khz(_dshot_mode), wide, rtl8735b_dshot_cpu_hz(),
                                                &_dshot_timing) == 0;
}

// 切成 DShot：第一次先停掉 PWM 的輸出、把腳改成 GPIO（失敗就停用這一路並說明原因）。所有 DShot 通道同時送，
// 速率只能一種，後來的速率套用到全部 DShot 通道。
void RCOutput::_set_dshot_mode(uint8_t chan, enum output_mode mode)
{
    const uint32_t bit = 1UL << chan;
    _pending_mask &= ~bit;
    _pending_us[chan] = 0;
    _has_valid_write[chan] = false;
    _value_us[chan] = 0;
    if ((_dshot_attached_mask & bit) == 0) {
        _stop_channel(chan);
        const int slot = rtl8735b_dshot_attach(rtl8735b_rcout_pin(chan));
        if (slot < 0) {
            _report_dshot_failure(chan, slot);
            _mode[chan] = MODE_PWM_NONE;
            return;
        }
        _dshot_slot[chan] = (int8_t)slot;
        _dshot_attached_mask |= bit;
    }
    _dshot_mask |= bit;
    _fast_mask |= bit;
    _dshot_mode = mode;
    for (uint8_t i = 0; i < CHANNELS; i++) {
        if (_is_dshot(i)) {
            _mode[i] = mode;
        }
    }
    _update_dshot_timing();
}

// 各 DShot 通道依目前的脈寬同時送一幀。安全開關關著時不送任何幀；未解鎖時送數值 0（停止）；脈寬 0 的通道不送。
// 遙測請求只帶在下一幀，送出後清掉。有排隊的命令時，命令的通道送命令（帶遙測位元），其他 DShot 通道送 0，
// 同 ArduPilot ChibiOS 的 RCOutput_serial.cpp。resend：計時器補送；主迴圈停住超過 MAIN_LOOP_STALE_MS 時油門幀送 0。
void RCOutput::_dshot_send_locked(bool resend)
{
    if (_dshot_mask == 0 || _dshot_stopped || !_dshot_timing_valid ||
        _safety_state == AP_HAL::Util::SAFETY_DISARMED) {
        return;
    }
    const uint8_t armed = hal.util->get_soft_armed() ? 1 : 0;
    const bool stale = resend && rtl8735b_ardupilot_ms_since_main_loop() > MAIN_LOOP_STALE_MS;
    DshotCommand *const command = _dshot_command_count != 0 ? &_dshot_commands[_dshot_command_head] : nullptr;
    uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS] = {};
    uint32_t active = 0;
    uint32_t sent = 0;
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        if (!_is_dshot(chan) || _dshot_slot[chan] < 0) {
            continue;
        }
        const uint8_t slot = (uint8_t)_dshot_slot[chan];
        if (command != nullptr) {
            const bool target = (command->chan_mask & (1UL << chan)) != 0;
            frames[slot] = rtl8735b_dshot_frame(target ? command->command : 0, target ? 1 : 0);
        } else {
            if (_value_us[chan] == 0) {
                continue;
            }
            const uint8_t telem = (_telem_request_mask & (1UL << chan)) != 0;
            const uint16_t value = stale ? 0 : rtl8735b_dshot_throttle_value(_value_us[chan], armed);
            frames[slot] = rtl8735b_dshot_frame(value, telem);
            sent |= 1UL << chan;
        }
        active |= 1UL << slot;
    }
    if (active == 0) {
        return;
    }
    const int status = rtl8735b_dshot_send(frames, active, &_dshot_timing);
    _dshot_sent = true;
    _dshot_last_send_us = AP_HAL::micros();
    if (status == RTL8735B_DSHOT_ESTOPPED) {
        _dshot_stopped = true;
        return;
    }
    if (status == RTL8735B_DSHOT_ETIMING && !_dshot_dropped_reported) {
        // 腳已拉低、這一幀作廢；下一次輸出或計時器補送時照常送（作廢的幀數見 rtl8735b_dshot_dropped_frames）
        _dshot_dropped_reported = true;
        RTL8735B::report_warning("RTL8735B: DShot frame dropped: CPU cycle counter stalled or jumped mid-frame");
    }
    if (status != RTL8735B_DSHOT_OK) {
        // 沒送出：遙測請求與命令的次數都留到下一幀
        return;
    }
    _telem_request_mask &= ~sent;
    if (command != nullptr && --command->frames_left == 0) {
        _dshot_command_head = (uint8_t)((_dshot_command_head + 1U) % DSHOT_COMMAND_QUEUE);
        _dshot_command_count--;
    }
}

// 計時器執行緒（較主執行緒優先），每 1 ms：
// - 解鎖中主迴圈停住超過 MAIN_LOOP_STALE_MS：所有 PWM 通道停止輸出（一般 PWM 停用、有刷以 duty 0 拉低），
//   只做一次；主迴圈恢復後第一次 push 再恢復（push 的 _pwm_stalled）。
// - DShot：主程式太久沒 push 時補送；有命令在排隊時每毫秒補一幀，讓命令照次數送完；主迴圈停住太久時補送的
//   油門幀是 0。
// 這裡不提示：主迴圈可能正卡在拿著別的鎖的地方。號誌被拿著就跳過，不等。
void RCOutput::_timer_tick()
{
    if (!_semaphore.take_nonblocking()) {
        return;
    }
    if (!_pwm_stalled && hal.util->get_soft_armed() &&
        rtl8735b_ardupilot_ms_since_main_loop() > MAIN_LOOP_STALE_MS) {
        _pwm_stalled = true;
        for (uint8_t chan = 0; chan < CHANNELS; chan++) {
            _stop_channel(chan);   // DShot 的腳在 _stop_channel 裡跳過，由下面的補送處理
        }
    }
    const uint32_t interval_us = _dshot_command_count != 0 ? DSHOT_COMMAND_INTERVAL_US : DSHOT_KEEPALIVE_US;
    if ((_dshot_sent || _dshot_command_count != 0) &&
        (uint32_t)(AP_HAL::micros() - _dshot_last_send_us) >= interval_us) {
        _dshot_send_locked(true);
    }
    _semaphore.give();
}

// 命令幀取代油門幀（解鎖後只收 priority 命令）。次數至少 10、至少 repeat_count；有逾時時至少逾時的毫秒數
// （命令排隊時約每毫秒送一幀）。佇列滿了就丟掉新的命令。
void RCOutput::send_dshot_command(uint8_t command, uint8_t chan, uint32_t command_timeout_ms,
                                  uint16_t repeat_count, bool priority)
{
    RCLock lock(_semaphore);
    if (!priority && hal.util->get_soft_armed()) {
        return;
    }
    uint32_t chan_mask;
    if (chan == ALL_CHANNELS) {
        chan_mask = _dshot_mask;
    } else if (chan < CHANNELS && _is_dshot(chan)) {
        chan_mask = 1UL << chan;
    } else {
        return;
    }
    if (chan_mask == 0 || _dshot_command_count >= DSHOT_COMMAND_QUEUE) {
        return;
    }
    uint32_t frames = repeat_count > 10U ? repeat_count : 10U;
    if (command_timeout_ms > frames) {
        frames = command_timeout_ms;
    }
    DshotCommand &slot = _dshot_commands[(_dshot_command_head + _dshot_command_count) % DSHOT_COMMAND_QUEUE];
    slot.command = command;
    slot.chan_mask = chan_mask;
    slot.frames_left = frames;
    _dshot_command_count++;
}

// 開機橫幅：連續同模式的通道合成一段（同 ArduPilot ChibiOS 的 RCOutput::get_output_mode_banner）
bool RCOutput::get_output_mode_banner(char banner_msg[], uint8_t banner_msg_len) const
{
    RCLock lock(_semaphore);
    bool any = false;
    for (uint8_t chan = 0; chan < CHANNELS; chan++) {
        any |= _mode[chan] != MODE_PWM_NONE;
    }
    if (!any) {
        hal.util->snprintf(banner_msg, banner_msg_len, "RCOut: None");
        return true;
    }
    hal.util->snprintf(banner_msg, banner_msg_len, "RCOut:");
    uint8_t low = 0;
    for (uint8_t chan = 1; chan <= CHANNELS; chan++) {
        if (chan == CHANNELS || _mode[chan] != _mode[low]) {
            if (_mode[low] != MODE_PWM_NONE) {
                append_to_banner(banner_msg, banner_msg_len, _mode[low], low + 1, chan);
            }
            low = chan;
        }
    }
    return true;
}

void RCOutput::set_telem_request_mask(uint32_t mask)
{
    RCLock lock(_semaphore);
    _telem_request_mask |= mask;
}

// 幀在每次 push 時送出（主迴圈的頻率），沒有 push 時由計時器補送；ArduPilot 以迴圈頻率倍數排的送幀頻率不用。
void RCOutput::set_dshot_rate(uint8_t dshot_rate, uint16_t loop_rate_hz)
{
    (void)dshot_rate;
    (void)loop_rate_hz;
}

void RCOutput::set_dshot_esc_type(DshotEscType esc_type)
{
    RCLock lock(_semaphore);
    _dshot_esc_type = esc_type;
    if (_dshot_mask != 0) {
        _update_dshot_timing();
    }
}

AP_HAL::RCOutput::DshotEscType RCOutput::get_dshot_esc_type() const
{
    RCLock lock(_semaphore);
    return _dshot_esc_type;
}
