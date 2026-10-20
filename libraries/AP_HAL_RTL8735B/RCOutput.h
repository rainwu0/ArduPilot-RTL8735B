/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <AP_HAL/RCOutput.h>
#include <AP_HAL/Util.h>
#include "AP_HAL_RTL8735B_Namespace.h"
#include "IOConfig.h"
#include "Semaphores.h"
#include "dshot_frame.h"

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

    // DShot：dshot_shim.c 以 GPIO 送幀
    void set_telem_request_mask(uint32_t mask) override;
    void set_dshot_rate(uint8_t dshot_rate, uint16_t loop_rate_hz) override;
    void set_dshot_esc_type(DshotEscType esc_type) override;
    DshotEscType get_dshot_esc_type() const override;
    void send_dshot_command(uint8_t command, uint8_t chan = ALL_CHANNELS, uint32_t command_timeout_ms = 0,
                            uint16_t repeat_count = 10, bool priority = false) override;
    bool get_output_mode_banner(char banner_msg[], uint8_t banner_msg_len) const override;

private:
    // 路數來自開發板 hwdef 的 RTL8735B_RCOUT
    static constexpr uint8_t CHANNELS = RTL8735B_RCOUT_CHANNELS;
    void _stop_channel(uint8_t chan);
    bool _brushed_hold_low(uint8_t chan);
    void _apply_channel(uint8_t chan);
    void _forget_outputs();
    void _commit_write(uint8_t chan, uint16_t pulse_us);
    void _refresh_rate(uint8_t chan);
    void _report_rate(uint8_t chan);
    void _report_clock_conflict(uint8_t chan);
    void _set_freq_locked(uint32_t chmask, uint16_t freq_hz, bool mark_fast);
    bool _channel_valid(uint8_t chan) const;
    bool _output_usable(uint8_t chan) const;
    bool _pulse_valid(uint8_t chan, uint16_t pulse_us) const;

    bool _is_dshot(uint8_t chan) const;
    void _set_dshot_mode(uint8_t chan, enum output_mode mode);
    void _report_dshot_failure(uint8_t chan, int status);
    void _update_dshot_timing();
    // resend：計時器補送（不是主程式的 push）
    void _dshot_send_locked(bool resend = false);
    // 計時器每 1 ms：主迴圈停住時停 PWM、DShot 的補送
    void _timer_tick();

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
    // 以 set_freq 設為高於 50 Hz 或設為有刷、OneShot125、DShot 模式的通道；set_default_rate 不改動它們。
    uint32_t _fast_mask;
    // 已回報「頻率不支援、寫入被拒」的通道；該通道收到支援的頻率後清除，之後再失敗會再回報。
    uint32_t _rate_reported_mask;
    // 已回報「PWM SCLK 被另一種模式（有刷或 OneShot125）占用」的通道。
    uint32_t _clock_reported_mask;
    bool _corked;
    bool _initialized;
    AP_HAL::Util::safety_state _safety_state;
    mutable Semaphore _semaphore;

    // dshot_shim 的槽位；-1 表示沒接上
    int8_t _dshot_slot[CHANNELS];
    // 正在送 DShot 的通道
    uint32_t _dshot_mask;
    // 腳已改成 GPIO 的通道：本次開機內不回 PWM
    uint32_t _dshot_attached_mask;
    // 已回報 DShot 無法使用或需要重開機的通道
    uint32_t _dshot_reported_mask;
    uint32_t _telem_request_mask;
    enum output_mode _dshot_mode;
    DshotEscType _dshot_esc_type;
    rtl8735b_dshot_timing_t _dshot_timing;
    bool _dshot_timing_valid;
    // 主迴圈停住、計時器已忘掉舊值並停掉 PWM；主迴圈時鐘回到上限內才清掉
    bool _stall_latched;
    bool _dshot_sent;
    bool _dshot_stopped;
    // 已提示過作廢的幀（rtl8735b_dshot_send 回 ETIMING），本次開機只提示一次
    bool _dshot_dropped_reported;
    uint32_t _dshot_last_send_us;

    // 排隊等送的 DShot 命令（同 ArduPilot ChibiOS 的佇列深度 8）
    struct DshotCommand {
        uint8_t command;
        uint32_t chan_mask;
        uint32_t frames_left;
    };
    static constexpr uint8_t DSHOT_COMMAND_QUEUE = 8;
    DshotCommand _dshot_commands[DSHOT_COMMAND_QUEUE];
    uint8_t _dshot_command_head;
    uint8_t _dshot_command_count;
};
