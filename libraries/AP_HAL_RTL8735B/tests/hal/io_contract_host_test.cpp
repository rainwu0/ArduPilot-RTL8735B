#include "IOConfig.h"
#include "RCOutput.h"
#include "dshot_frame.h"
#include "dshot_shim.h"
#include "io_shim.h"

#include <AP_HAL/AP_HAL.h>
#include <GCS_MAVLink/GCS.h>

#include <assert.h>
#include <condition_variable>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
std::recursive_mutex semaphore_mutex;
unsigned apply_count;
unsigned apply_ticks_count;
unsigned hold_low_count;
unsigned stop_count;
// 停止動作的先後：H＝以 duty 0 驅動（有刷的停止狀態），S＝rtl8735b_pwm_stop
std::string stop_events;
unsigned flush_count;
unsigned oneshot125_count;
int oneshot125_result;
int apply_ticks_result;
bool output_is_oneshot125[4];
uint16_t output_oneshot125_ticks[4];
bool output_active[4];
bool output_is_ticks[4];
bool output_low_driven[4];
uint16_t output_pulse[4];
uint16_t output_period_ticks[4];
uint16_t output_duty_ticks[4];

// DShot 送幀的替身：記下接上的腳與每次送出的幀
struct DshotSend {
    uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS];
    uint32_t active;
    rtl8735b_dshot_timing_t timing;
};
std::vector<uint32_t> dshot_pins;
std::vector<DshotSend> dshot_sends;
uint32_t dshot_fail_pin = 0xFFFFFFFFu;
int dshot_fail_code;
bool dshot_stopped;
int dshot_send_result = RTL8735B_DSHOT_OK;   // 沒有緊急停止時送幀回傳的值
uint32_t now_us;
AP_HAL::Scheduler fake_scheduler;
AP_HAL::Util fake_util;
AP_HAL::HAL make_fake_hal()
{
    AP_HAL::HAL h(&fake_scheduler);
    h.util = &fake_util;
    return h;
}
AP_HAL::HAL fake_hal_object = make_fake_hal();

// 把 stdout 暫時導到暫存檔，用來檢查 RCOutput 印出的診斷訊息。
class StdoutCapture {
public:
    StdoutCapture()
    {
        fflush(stdout);
        _saved_fd = dup(fileno(stdout));
        _file = tmpfile();
        assert(_saved_fd >= 0 && _file != nullptr);
        assert(dup2(fileno(_file), fileno(stdout)) >= 0);
    }
    std::string finish()
    {
        fflush(stdout);
        assert(dup2(_saved_fd, fileno(stdout)) >= 0);
        close(_saved_fd);
        std::string text;
        rewind(_file);
        char buffer[256];
        size_t length;
        while ((length = fread(buffer, 1, sizeof(buffer), _file)) > 0) {
            text.append(buffer, length);
        }
        fclose(_file);
        return text;
    }
private:
    int _saved_fd;
    FILE *_file;
};

void reset_fake_outputs()
{
    fake_statustext().clear();
    apply_count = 0;
    apply_ticks_count = 0;
    hold_low_count = 0;
    stop_count = 0;
    stop_events.clear();
    flush_count = 0;
    oneshot125_count = 0;
    oneshot125_result = 0;
    apply_ticks_result = 0;
    for (unsigned i = 0; i < 4; i++) {
        output_active[i] = false;
        output_is_ticks[i] = false;
        output_low_driven[i] = false;
        output_pulse[i] = 0;
        output_period_ticks[i] = 0;
        output_duty_ticks[i] = 0;
        output_is_oneshot125[i] = false;
        output_oneshot125_ticks[i] = 0;
    }
}
}

const AP_HAL::HAL &hal = fake_hal_object;

uint32_t AP_HAL::micros()
{
    return now_us;
}

// 主迴圈的時鐘（HAL_RTL8735B_Class.cpp 的 rtl8735b_ardupilot_ms_since_main_loop）：測試直接設距上一圈幾毫秒
uint32_t main_loop_ms_ago;

extern "C" uint32_t rtl8735b_ardupilot_ms_since_main_loop(void)
{
    return main_loop_ms_ago;
}

extern "C" int rtl8735b_dshot_attach(uint32_t pin)
{
    if (pin == dshot_fail_pin) {
        return dshot_fail_code;
    }
    for (size_t i = 0; i < dshot_pins.size(); i++) {
        if (dshot_pins[i] == pin) {
            return (int)i;
        }
    }
    dshot_pins.push_back(pin);
    return (int)dshot_pins.size() - 1;
}

extern "C" uint32_t rtl8735b_dshot_cpu_hz(void)
{
    return 500000000u;
}

extern "C" int rtl8735b_dshot_send(const uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS], uint32_t active,
                                   const rtl8735b_dshot_timing_t *timing)
{
    DshotSend send;
    memcpy(send.frames, frames, sizeof(send.frames));
    send.active = active;
    send.timing = *timing;
    dshot_sends.push_back(send);
    return dshot_stopped ? RTL8735B_DSHOT_ESTOPPED : dshot_send_result;
}

extern "C" void rtl8735b_dshot_emergency_stop(void)
{
    dshot_stopped = true;
}

namespace RTL8735B {
Semaphore::Semaphore() : _handle(nullptr) {}
bool Semaphore::give() { semaphore_mutex.unlock(); return true; }
bool Semaphore::take(uint32_t) { semaphore_mutex.lock(); return true; }
bool Semaphore::take_nonblocking() { return semaphore_mutex.try_lock(); }
void Semaphore::take_blocking() { semaphore_mutex.lock(); }
bool Semaphore::check_owner() { return true; }
}

extern "C" int rtl8735b_pwm_apply(uint8_t channel, uint16_t period_us, uint16_t pulse_us)
{
    assert(channel < 4);
    assert(period_us > pulse_us);
    apply_count++;
    output_active[channel] = true;
    output_is_ticks[channel] = false;
    output_is_oneshot125[channel] = false;
    output_low_driven[channel] = false;
    output_pulse[channel] = pulse_us;
    return 0;
}

extern "C" int rtl8735b_pwm_apply_oneshot125(uint8_t channel, uint16_t duty_ticks)
{
    assert(channel < 4);
    assert(duty_ticks > 0 && duty_ticks < RTL8735B_ONESHOT125_PERIOD_TICKS);
    oneshot125_count++;
    if (oneshot125_result != 0) {
        return oneshot125_result;
    }
    output_active[channel] = true;
    output_is_oneshot125[channel] = true;
    output_oneshot125_ticks[channel] = duty_ticks;
    return 0;
}

extern "C" int rtl8735b_pwm_apply_ticks(uint8_t channel, uint16_t period_ticks, uint16_t duty_ticks)
{
    assert(channel < 4);
    assert(period_ticks > 0 && period_ticks <= RTL8735B_PWM_MAX_TICKS);
    assert(duty_ticks < period_ticks);
    if (duty_ticks == 0) {
        hold_low_count++;
        stop_events += "H";
    } else {
        apply_ticks_count++;
    }
    if (apply_ticks_result != 0) {
        return apply_ticks_result;
    }
    output_active[channel] = duty_ticks != 0;
    output_low_driven[channel] = duty_ticks == 0;
    output_is_ticks[channel] = true;
    output_period_ticks[channel] = period_ticks;
    output_duty_ticks[channel] = duty_ticks;
    return 0;
}

extern "C" void rtl8735b_pwm_stop(uint8_t channel)
{
    assert(channel < 4);
    stop_count++;
    stop_events += "S";
    output_active[channel] = false;
    output_low_driven[channel] = false;
    output_pulse[channel] = 0;
    output_duty_ticks[channel] = 0;
    output_oneshot125_ticks[channel] = 0;
}

extern "C" void rtl8735b_pwm_flush(void)
{
    flush_count++;
}

static void test_board_pin_profile_and_whitelist()
{
    assert(rtl8735b_rcout_channel_count() == 4);
    assert(rtl8735b_rcout_pin(0) == ((5U << 5) | 14U));
    assert(rtl8735b_rcout_pin(1) == ((5U << 5) | 11U));
    assert(rtl8735b_rcout_pin(2) == ((5U << 5) | 12U));
    assert(rtl8735b_rcout_pin(3) == ((5U << 5) | 13U));
    assert(rtl8735b_rcout_pin(4) == RTL8735B_INVALID_PIN);
    assert(rtl8735b_analog_pin(2) == ((5U << 5) | 0U));
    assert(rtl8735b_analog_pin(1) == RTL8735B_INVALID_PIN);
    assert(rtl8735b_gpio_pin(7) == ((3U << 5) | 17U));
    assert(rtl8735b_gpio_pin(25) == ((4U << 5) | 6U));
    assert(rtl8735b_gpio_pin(26) == ((5U << 5) | 9U));
    assert(rtl8735b_gpio_pin(2) == RTL8735B_INVALID_PIN);
    assert(!rtl8735b_gpio_output_allowed(7));
    assert(rtl8735b_gpio_output_allowed(25));
    assert(rtl8735b_gpio_output_allowed(26));
}

static void test_rc_output_startup_and_safe_transition()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    assert(apply_count == 0);
    output.enable_ch(0);
    assert(apply_count == 0);
    output.write(0, 1500);
    assert(apply_count == 1);
    assert(output_active[0] && output_pulse[0] == 1500);

    assert(output.force_safety_on());
    assert(!output_active[0]);
    assert(output.read(0) == 0);
    const unsigned stopped_apply_count = apply_count;

    output.cork();
    output.write(0, 1600); // safety-on 時的寫入必須拒絕且不能加入佇列。
    output.force_safety_off();
    output.push();
    assert(apply_count == stopped_apply_count);
    assert(!output_active[0]);

    output.write(0, 1700); // 解除 safety 後必須收到新的寫入。
    assert(apply_count == stopped_apply_count + 1);
    assert(output_active[0] && output_pulse[0] == 1700);
}

static void test_frequency_change_drops_corked_write()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.enable_ch(1);
    output.cork();
    output.write(1, 1600);
    output.set_freq(1U << 1, 400);
    output.push();
    assert(apply_count == 0);
    assert(!output_active[1]);
    assert(output.read(1) == 0);
    assert(output.get_freq(1) == 400);
}

static void test_invalid_rate_pulse_and_mode_fail_closed()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.enable_ch(2);
    output.write(2, 1500);
    assert(apply_count == 1);
    assert(output_active[2]);

    output.set_freq(1U << 2, 0);
    assert(!output_active[2]);
    output.write(2, 1500);
    assert(apply_count == 1);

    output.set_freq(1U << 2, 50);
    output.write(2, 3000);
    assert(!output_active[2]);
    assert(apply_count == 1);

    output.set_output_mode(1U << 2, AP_HAL::RCOutput::MODE_PWM_DSHOT1200);
    uint32_t mode_mask = 0;
    assert(output.get_output_mode(mode_mask) == AP_HAL::RCOutput::MODE_PWM_NONE);
    assert(mode_mask == 0);
    output.write(2, 1500);
    assert(apply_count == 1);

    output.set_output_mode(1U << 2, AP_HAL::RCOutput::MODE_PWM_NORMAL);
    output.write(2, 1500);
    assert(apply_count == 2);
    assert(output_active[2]);
    assert(output.get_freq(4) == 0);
}

static void test_rc_output_safety_calls_under_thread_contention()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.enable_ch(0);
    output.force_safety_on();

    std::thread writer([&output]() {
        for (unsigned i = 0; i < 2000; i++) {
            output.cork();
            output.write(0, 1500);
            output.push();
        }
    });
    std::thread safety([&output]() {
        for (unsigned i = 0; i < 2000; i++) {
            output.force_safety_off();
            output.force_safety_on();
        }
    });
    writer.join();
    safety.join();

    output.force_safety_on();
    const unsigned applied_while_locked = apply_count;
    output.force_safety_off();
    output.push();
    assert(apply_count == applied_while_locked);
    assert(!output_active[0]);
    assert(output.read(0) == 0);
}

static void test_normal_and_brushed_rate_policy()
{
    // 一般 PWM：50–490 Hz，不因有刷支援而改變。
    assert(!rtl8735b_rc_rate_valid(0));
    assert(!rtl8735b_rc_rate_valid(49));
    assert(rtl8735b_rc_rate_valid(50));
    assert(rtl8735b_rc_rate_valid(490));
    assert(!rtl8735b_rc_rate_valid(491));
    assert(!rtl8735b_rc_rate_valid(16000));

    // 有刷：40 MHz SCLK、12 位元週期，10–32 kHz。
    assert(!rtl8735b_rc_brushed_rate_valid(0));
    assert(!rtl8735b_rc_brushed_rate_valid(490));
    assert(!rtl8735b_rc_brushed_rate_valid(9999));
    assert(rtl8735b_rc_brushed_rate_valid(10000));
    assert(rtl8735b_rc_brushed_rate_valid(16000));
    assert(rtl8735b_rc_brushed_rate_valid(32000));
    assert(!rtl8735b_rc_brushed_rate_valid(32001));

    assert(rtl8735b_rc_brushed_period_ticks(16000) == 2500);
    assert(rtl8735b_rc_brushed_period_ticks(10000) == 4000);
    assert(rtl8735b_rc_brushed_period_ticks(32000) == 1250);
    assert(rtl8735b_rc_brushed_period_ticks(12345) == 3240); // 40e6/12345 = 3240.2，四捨五入
    assert(rtl8735b_rc_brushed_period_ticks(9999) == 0);
    assert(rtl8735b_rc_brushed_period_ticks(50) == 0);
    assert(rtl8735b_rc_brushed_period_ticks(10000) <= RTL8735B_PWM_MAX_TICKS);
}

static void test_brushed_duty_mapping()
{
    // 與 ChibiOS／ESP32 HAL 相同：ESC 範圍內線性對應 duty，下限（含）以下為 0，上限（含）以上為全開。
    // 全開取 period - 1 tick，避開 DUTY 等於週期這個沒有文件的邊界。
    assert(rtl8735b_rc_brushed_duty_ticks(0, 1000, 2000, 2500) == 0);
    assert(rtl8735b_rc_brushed_duty_ticks(999, 1000, 2000, 2500) == 0);
    assert(rtl8735b_rc_brushed_duty_ticks(1000, 1000, 2000, 2500) == 0);
    assert(rtl8735b_rc_brushed_duty_ticks(1001, 1000, 2000, 2500) == 2);
    assert(rtl8735b_rc_brushed_duty_ticks(1500, 1000, 2000, 2500) == 1250);
    assert(rtl8735b_rc_brushed_duty_ticks(1999, 1000, 2000, 2500) == 2497);
    assert(rtl8735b_rc_brushed_duty_ticks(2000, 1000, 2000, 2500) == 2499);
    assert(rtl8735b_rc_brushed_duty_ticks(2200, 1000, 2000, 2500) == 2499);
    assert(rtl8735b_rc_brushed_duty_ticks(1, 0, 2000, 1) == 0);
    assert(rtl8735b_rc_brushed_duty_ticks(1550, 1100, 1900, 4000) == 2250);
    // 範圍或週期不合法時一律 0（不輸出）。
    assert(rtl8735b_rc_brushed_duty_ticks(1500, 2000, 2000, 2500) == 0);
    assert(rtl8735b_rc_brushed_duty_ticks(1500, 2000, 1000, 2500) == 0);
    assert(rtl8735b_rc_brushed_duty_ticks(1500, 1000, 2000, 0) == 0);
    assert(rtl8735b_rc_brushed_duty_ticks(1500, 1000, 2000, RTL8735B_PWM_MAX_TICKS + 1) == 0);
}

// AP_Motors::rc_set_freq 的呼叫順序：先 set_freq(RC_SPEED)，再 set_output_mode(BRUSHED)。
static void setup_brushed(RTL8735B::RCOutput &output, uint32_t mask, uint16_t freq_hz)
{
    output.set_freq(mask, freq_hz);
    output.set_output_mode(mask, AP_HAL::RCOutput::MODE_PWM_BRUSHED);
}

static void test_init_does_not_touch_pins()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.init();
    assert(apply_count == 0 && apply_ticks_count == 0 && hold_low_count == 0 && stop_count == 0);
    for (uint8_t chan = 0; chan < 4; chan++) {
        assert(!output_active[chan] && !output_low_driven[chan]);
        assert(output.get_freq(chan) == 50);
    }
}

static void test_brushed_mode_outputs_duty_and_zero_at_min()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    setup_brushed(output, 0x0FU, 16000);
    uint32_t mode_mask = 0;
    assert(output.get_output_mode(mode_mask) == AP_HAL::RCOutput::MODE_PWM_BRUSHED);
    assert(mode_mask == 0x0FU);
    // 進入有刷模式即以 duty 0 主動驅動四腳（尚未 enable_ch、尚未寫入）。
    for (uint8_t chan = 0; chan < 4; chan++) {
        assert(output_low_driven[chan] && !output_active[chan]);
        assert(output_period_ticks[chan] == 2500);
    }
    // 有刷通道之後的停止一律是 duty 0，不再走一般 PWM 的停用路徑。
    const unsigned stops_after_setup = stop_count;
    for (uint8_t chan = 0; chan < 4; chan++) {
        assert(output.get_freq(chan) == 16000);
        output.enable_ch(chan);
    }
    output.set_esc_scaling(1000, 2000);
    assert(apply_count == 0 && apply_ticks_count == 0);

    // 未解鎖時 AP_Motors 送 0 或 MOT_PWM_MIN，兩者都必須保持 duty 0。
    output.write(0, 0);
    assert(output_low_driven[0] && !output_active[0]);
    output.write(0, 1000);
    assert(apply_ticks_count == 0);
    assert(output_low_driven[0] && !output_active[0]);

    output.write(0, 1500);
    assert(apply_ticks_count == 1);
    assert(apply_count == 0);
    assert(output_active[0] && output_is_ticks[0]);
    assert(output_period_ticks[0] == 2500);
    assert(output_duty_ticks[0] == 1250);
    assert(output.read(0) == 1500);

    output.write(0, 2000);
    assert(output_duty_ticks[0] == 2499);

    // 回到 MOT_PWM_MIN：duty 0，腳位持續被驅動為低。
    output.write(0, 1000);
    assert(!output_active[0] && output_low_driven[0]);
    assert(output_duty_ticks[0] == 0);
    assert(output.read(0) == 1000);
    assert(stop_count == stops_after_setup);

    // cork/push 路徑同樣換算。
    output.cork();
    output.write(1, 1250);
    output.write(2, 1750);
    assert(apply_ticks_count == 2);
    output.push();
    assert(apply_ticks_count == 4);
    assert(output_duty_ticks[1] == 625);
    assert(output_duty_ticks[2] == 1875);
}

static void test_brushed_safety_forces_zero_and_is_not_replayed()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    setup_brushed(output, 0x0FU, 16000);
    const unsigned stops_after_setup = stop_count;
    output.enable_ch(3);
    output.write(3, 1800);
    assert(output_active[3]);

    assert(output.force_safety_on());
    assert(!output_active[3]);
    for (uint8_t chan = 0; chan < 4; chan++) {
        assert(output_low_driven[chan]);
    }
    assert(output.read(3) == 0);
    const unsigned applied = apply_ticks_count;
    output.write(3, 1800);
    output.cork();
    output.write(3, 1900);
    output.force_safety_off();
    output.push();
    assert(apply_ticks_count == applied);
    assert(!output_active[3]);

    output.write(3, 1100);
    assert(apply_ticks_count == applied + 1);
    assert(output_duty_ticks[3] == 250);

    output.disable_ch(3);
    assert(!output_active[3] && output_low_driven[3]);
    output.write(3, 1500);
    assert(!output_active[3] && output_low_driven[3]);
    assert(stop_count == stops_after_setup);
}

static void test_brushed_rejects_unsupported_rate_fail_closed()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    // 有刷模式但頻率在支援範圍外（例如沿用 RC_SPEED=490）：不輸出，也不偷偷換成別的頻率。
    setup_brushed(output, 1U << 0, 490);
    // 頻率不支援時仍以 duty 0 驅動腳位（週期取 10 kHz，duty 0 時週期不影響輸出）。
    assert(output_low_driven[0] && output_period_ticks[0] == 4000);
    output.enable_ch(0);
    output.write(0, 1500);
    assert(apply_ticks_count == 0 && apply_count == 0);
    assert(!output_active[0] && output_low_driven[0]);

    output.set_freq(1U << 0, 40000);
    output.write(0, 1500);
    assert(apply_ticks_count == 0 && apply_count == 0);

    output.set_freq(1U << 0, 20000);
    assert(output.get_freq(0) == 20000);
    output.write(0, 1500);
    assert(apply_ticks_count == 1);
    assert(output_period_ticks[0] == 2000);
    assert(output_duty_ticks[0] == 1000);

    // 從有刷切回一般 PWM：20 kHz 對一般 PWM 不合法，必須停止輸出。
    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_NORMAL);
    assert(!output_active[0]);
    output.write(0, 1500);
    assert(apply_count == 0);
    assert(!output_active[0]);
}

static void test_brushed_rate_reported_only_when_write_rejected()
{
    reset_fake_outputs();
    {
        // Copter 開機順序：AP_MotorsMatrix::init 以 400 Hz 呼叫 rc_set_freq（set_freq 後切有刷模式），
        // enable_aux_servos，init_rc_out 再以 RC_SPEED 呼叫一次。過渡的 400 Hz 不得印出訊息。
        StdoutCapture capture;
        RTL8735B::RCOutput output;
        output.init();
        setup_brushed(output, 0x0FU, 400);
        for (uint8_t chan = 0; chan < 4; chan++) {
            output.enable_ch(chan);
        }
        setup_brushed(output, 0x0FU, 16000);
        output.set_esc_scaling(1000, 2000);
        output.write(0, 1000);
        output.write(0, 1500);
        const std::string text = capture.finish();
        assert(text.empty());
        assert(output_active[0] && output_duty_ticks[0] == 1250);
    }
    reset_fake_outputs();
    {
        // RC_SPEED 超出範圍：馬達通道第一次被拒的寫入（上鎖時的 MOT_PWM_MIN）印出實際頻率，每通道一次；
        // 通道收到支援的頻率後清除，之後再出錯會再印。輸出一律維持 duty 0。
        StdoutCapture capture;
        RTL8735B::RCOutput output;
        output.init();
        setup_brushed(output, 0x03U, 400);
        output.enable_ch(0);
        output.enable_ch(1);
        setup_brushed(output, 0x03U, 9000);
        output.write(0, 1000);
        output.write(0, 1500);
        output.cork();
        output.write(1, 1000);
        output.push();
        assert(!output_active[0] && output_low_driven[0]);
        assert(!output_active[1] && output_low_driven[1]);
        output.set_freq(1U << 0, 16000);
        output.write(0, 1500);
        assert(output_active[0] && output_duty_ticks[0] == 1250);
        output.set_freq(1U << 0, 40000);
        output.write(0, 1500);
        assert(!output_active[0] && output_low_driven[0]);
        const std::string text = capture.finish();
        assert(text ==
               "RTL8735B: brushed PWM ch1 9000 Hz unsupported (10000-32000 Hz); output held at zero\n"
               "RTL8735B: brushed PWM ch2 9000 Hz unsupported (10000-32000 Hz); output held at zero\n"
               "RTL8735B: brushed PWM ch1 40000 Hz unsupported (10000-32000 Hz); output held at zero\n");
        // 同樣的文字也以 STATUSTEXT（警告）送給地面站
        assert(fake_statustext() ==
               "[4] RTL8735B: brushed PWM ch1 9000 Hz unsupported (10000-32000 Hz); output held at zero\n"
               "[4] RTL8735B: brushed PWM ch2 9000 Hz unsupported (10000-32000 Hz); output held at zero\n"
               "[4] RTL8735B: brushed PWM ch1 40000 Hz unsupported (10000-32000 Hz); output held at zero\n");
    }
}

static void test_set_default_rate_keeps_motor_channels()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    // 通道 0–1 為有刷馬達，通道 2 為 400 Hz 一般 PWM 馬達，通道 3 為預設速率的伺服。
    setup_brushed(output, 0x03U, 16000);
    output.set_freq(1U << 2, 400);
    for (uint8_t chan = 0; chan < 4; chan++) {
        output.enable_ch(chan);
    }
    output.write(0, 1500);
    output.write(2, 1500);
    output.write(3, 1500);
    const unsigned ticks = apply_ticks_count;
    const unsigned holds = hold_low_count;
    const unsigned pwm = apply_count;
    const unsigned stops = stop_count;

    // SRV_Channels::enable_aux_servos 每秒呼叫 set_default_rate(SERVO_RATE)；
    // 與 ChibiOS／ESP32 HAL 相同，不得改動以 set_freq 設為高速或有刷的通道。
    output.set_default_rate(50);
    assert(output.get_freq(0) == 16000);
    assert(output.get_freq(2) == 400);
    assert(output.get_freq(3) == 50);
    assert(output_active[0] && output_active[2] && output_active[3]);
    assert(stop_count == stops && hold_low_count == holds);
    assert(apply_ticks_count == ticks && apply_count == pwm);

    // 預設速率真的改變時，非高速通道照舊改頻並等待新的寫入。
    output.set_default_rate(100);
    assert(output.get_freq(3) == 100);
    assert(!output_active[3]);
    assert(output.get_freq(0) == 16000 && output_active[0]);
    assert(output.get_freq(2) == 400 && output_active[2]);
    output.write(3, 1500);
    assert(output_active[3] && output_pulse[3] == 1500);
}

static void test_normal_and_oneshot_paths_unchanged()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.set_freq(0x0FU, 400);
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_NORMAL);
    output.enable_ch(0);
    output.set_esc_scaling(1100, 1900);
    // 一般 PWM 直接輸出脈寬，不套用 ESC 範圍。
    output.write(0, 1000);
    assert(apply_count == 1 && apply_ticks_count == 0);
    assert(output_active[0] && !output_is_ticks[0] && output_pulse[0] == 1000);
    output.write(0, 2100);
    assert(output_pulse[0] == 2100);
    output.write(0, 2101);
    assert(!output_active[0]);

    // 一般 PWM 不接受有刷頻率。
    output.set_freq(1U << 1, 16000);
    output.enable_ch(1);
    output.write(1, 1500);
    assert(!output_active[1]);
    assert(apply_count == 2);

    // DShot1200 不支援：停止輸出並回報為 NONE。
    output.write(0, 1500);
    assert(output_active[0]);
    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_DSHOT1200);
    assert(!output_active[0]);
    uint32_t mode_mask = 0;
    assert(output.get_output_mode(mode_mask) == AP_HAL::RCOutput::MODE_PWM_NONE);
    assert(mode_mask == 0);
    output.write(0, 1500);
    assert(!output_active[0]);
    assert(apply_ticks_count == 0);
}

// io_shim 在硬體還沒套用上一次設定時延後寫入；push() 每次都要讓它補寫，
// 即使這次沒有新的寫入（值沒變時 Copter 仍每個迴圈 push）。
static void test_push_flushes_deferred_pwm_writes()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.enable_ch(0);
    output.cork();
    output.write(0, 1500);
    output.push();
    assert(flush_count == 1);
    output.cork();
    output.push();
    assert(flush_count == 2);
    // 沒有 cork 的 push 也補寫：不經 cork 直接 write() 而被延後的值，不能停在舊值。
    output.push();
    assert(flush_count == 3);
}

// OneShot125：4 MHz PWM SCLK、固定 1 kHz 連續輸出；1000–2000 us 對應 125–250 us（pulse / 2 個 tick）。
static void test_oneshot125_mapping()
{
    assert(RTL8735B_ONESHOT125_HZ == 1000U);
    assert(RTL8735B_ONESHOT125_PERIOD_TICKS == 4000U);
    assert(rtl8735b_rc_oneshot125_duty_ticks(1000) == 500);
    assert(rtl8735b_rc_oneshot125_duty_ticks(1501) == 750);
    assert(rtl8735b_rc_oneshot125_duty_ticks(2000) == 1000);
    assert(rtl8735b_rc_oneshot125_duty_ticks(400) == 200);
    assert(rtl8735b_rc_oneshot125_duty_ticks(2100) == 1050);
    assert(rtl8735b_rc_oneshot125_duty_ticks(399) == 0);
    assert(rtl8735b_rc_oneshot125_duty_ticks(2101) == 0);
    assert(rtl8735b_rc_oneshot125_duty_ticks(0) == 0);
}

// AP_Motors::rc_set_freq 的呼叫順序：先 set_freq(RC_SPEED)，再 set_output_mode。
static void test_oneshot125_mode_outputs_scaled_pulse()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.set_freq(1U << 0, 490);
    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_ONESHOT125);
    output.enable_ch(0);
    assert(output.get_freq(0) == RTL8735B_ONESHOT125_HZ);
    uint32_t mask = 0;
    assert(output.get_output_mode(mask) == AP_HAL::RCOutput::MODE_PWM_ONESHOT125 && mask == 1U);

    // 脈寬直接換算，不套用 ESC 範圍（與 ChibiOS 相同）。
    output.set_esc_scaling(1100, 1900);
    output.write(0, 1500);
    assert(oneshot125_count == 1 && apply_count == 0 && apply_ticks_count == 0);
    assert(output_is_oneshot125[0] && output_oneshot125_ticks[0] == 750);
    output.cork();
    output.write(0, 1100);
    output.push();
    assert(output_oneshot125_ticks[0] == 550);
    assert(output.read(0) == 1100);

    // 頻率由這個模式決定：之後的 set_freq 與 set_default_rate 都不改變輸出。
    output.set_freq(1U << 0, 490);
    output.write(0, 2000);
    assert(output_active[0] && output_oneshot125_ticks[0] == 1000);
    assert(output.get_freq(0) == RTL8735B_ONESHOT125_HZ);
    output.set_default_rate(50);
    assert(output_active[0] && output.get_freq(0) == RTL8735B_ONESHOT125_HZ);

    // 範圍外的脈寬與 safety 都停止輸出。
    output.write(0, 2101);
    assert(!output_active[0] && output.read(0) == 0);
    output.write(0, 1500);
    assert(output_active[0]);
    output.force_safety_on();
    assert(!output_active[0]);
}

// 有刷與 OneShot125 共用 PWM SCLK，同一次開機只能用一種；io_shim 拒絕時每個通道印一次並停止輸出。
static void test_oneshot125_clock_conflict_is_reported_once()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.set_freq(1U << 1, 490);
    output.set_output_mode(1U << 1, AP_HAL::RCOutput::MODE_PWM_ONESHOT125);
    output.enable_ch(1);
    oneshot125_result = RTL8735B_PWM_ERR_CLOCK;
    StdoutCapture capture;
    output.write(1, 1500);
    output.write(1, 1600);
    // 其他原因的失敗（例如緊急停止後）只停止輸出，不印時鐘訊息。
    output.enable_ch(2);
    output.set_output_mode(1U << 2, AP_HAL::RCOutput::MODE_PWM_ONESHOT125);
    oneshot125_result = -1;
    output.write(2, 1500);
    const std::string text = capture.finish();
    assert(!output_active[1] && output.read(1) == 0);
    assert(!output_active[2] && output.read(2) == 0);
    assert(text == "RTL8735B: OneShot125 ch2 unavailable (PWM clock in use by brushed output); output stopped\n");
}

// 反方向：OneShot125 已占用 PWM SCLK 時，有刷也印出時鐘衝突（每個通道一次），而不是「無法設為零 duty」。
static void test_brushed_clock_conflict_is_reported_once()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    apply_ticks_result = RTL8735B_PWM_ERR_CLOCK;
    StdoutCapture capture;
    setup_brushed(output, 1U << 0, 16000);
    output.enable_ch(0);
    output.write(0, 1500);
    output.write(0, 1600);
    const std::string text = capture.finish();
    assert(!output_active[0] && output.read(0) == 0);
    assert(text == "RTL8735B: brushed PWM ch1 unavailable (PWM clock in use by OneShot125 output); output stopped\n");
}

// 從有刷切到一般 PWM：切換當下照舊模式停止，先以 duty 0 驅動（有刷的停止狀態），不直接停用通道；
// SDK 沒寫停用後腳位的電位，低側開關的閘極不能處於不確定的電位。
static void test_brushed_to_normal_switch_holds_low_first()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    setup_brushed(output, 1U << 0, 16000);
    output.enable_ch(0);
    output.write(0, 1600);
    assert(output_active[0] && output_duty_ticks[0] != 0);
    stop_events.clear();
    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_NORMAL);
    assert(!stop_events.empty() && stop_events[0] == 'H');
    assert(!output_active[0]);
}

// 通道正在輸出 OneShot125 時，同一次開機直接切成有刷：PWM 時脈還被 OneShot125 占用，
// 有刷的 duty 0 寫不進去（替身一律回時脈衝突）。換模式本身就要把舊的 OneShot125 輸出停掉，
// 之後不能讓輸出重新開始；有刷停止時 duty 0 寫不進去，也要退回直接停用通道。
static void test_oneshot125_to_brushed_switch_stops_old_output()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    // 先要求有刷可用的頻率，再切成 OneShot125 並開始輸出
    // （與 AP_Motors 的順序相同：set_freq 之後才 set_output_mode）。
    output.set_freq(1U << 0, 16000);
    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_ONESHOT125);
    output.enable_ch(0);
    output.write(0, 1500);
    assert(output_active[0] && output_is_oneshot125[0] && output_oneshot125_ticks[0] == 750);
    assert(output.read(0) == 1500);

    // 有刷的寫入一律因時脈衝突失敗；診斷訊息的內容由 test_brushed_clock_conflict_is_reported_once 檢查。
    apply_ticks_result = RTL8735B_PWM_ERR_CLOCK;
    StdoutCapture capture;
    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_BRUSHED);
    // 切換本身就讓舊輸出停止，不能留到下一次寫入。
    assert(!output_active[0]);
    assert(output.read(0) == 0);
    uint32_t mode_mask = 0;
    assert(output.get_output_mode(mode_mask) == AP_HAL::RCOutput::MODE_PWM_BRUSHED && mode_mask == 1U);

    // 之後的寫入：有刷寫不進去，輸出不能重新開始，也不能回到 OneShot125。
    const unsigned oneshot125_before = oneshot125_count;
    output.write(0, 1500);
    assert(!output_active[0] && output.read(0) == 0);
    assert(oneshot125_count == oneshot125_before);

    // 即使通道又被別的路徑設成輸出中（停止時 duty 0 寫不進去，不能只相信自己記的狀態），
    // 安全開關仍要讓它真的停止，且讀回 0。
    output_active[0] = true;
    assert(output.force_safety_on());
    assert(!output_active[0]);
    assert(output.read(0) == 0);
    (void)capture.finish();
}

// OneShot：與一般 PWM 相同的微秒路徑，以 set_freq 的頻率連續輸出（50–490 Hz）。
// 頻率不支援時寫入被拒，每個通道印一次。
static void test_oneshot_mode_uses_normal_pwm_path()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    output.set_freq(1U << 2, 400);
    output.set_output_mode(1U << 2, AP_HAL::RCOutput::MODE_PWM_ONESHOT);
    output.enable_ch(2);
    output.write(2, 1500);
    assert(apply_count == 1 && output_active[2] && output_pulse[2] == 1500);
    uint32_t mask = 0;
    assert(output.get_output_mode(mask) == AP_HAL::RCOutput::MODE_PWM_ONESHOT && mask == (1U << 2));
    assert(output.get_freq(2) == 400);

    output.set_freq(1U << 2, 1000);
    StdoutCapture capture;
    output.write(2, 1500);
    output.write(2, 1500);
    const std::string text = capture.finish();
    assert(!output_active[2] && apply_count == 1);
    assert(text == "RTL8735B: PWM ch3 1000 Hz unsupported (50-490 Hz); output stopped\n");
}

namespace {
void reset_dshot()
{
    dshot_pins.clear();
    dshot_sends.clear();
    dshot_fail_pin = 0xFFFFFFFFu;
    dshot_fail_code = 0;
    dshot_stopped = false;
    dshot_send_result = RTL8735B_DSHOT_OK;
    now_us = 1000;
    main_loop_ms_ago = 0;
    fake_util.soft_armed = false;
}

rtl8735b_dshot_timing_t timing_for(uint16_t rate_khz, uint8_t wide)
{
    rtl8735b_dshot_timing_t t;
    assert(rtl8735b_dshot_timing(rate_khz, wide, 500000000u, &t) == 0);
    return t;
}

bool same_timing(const rtl8735b_dshot_timing_t &a, const rtl8735b_dshot_timing_t &b)
{
    return a.bit_cycles == b.bit_cycles && a.t0h_cycles == b.t0h_cycles && a.t1h_cycles == b.t1h_cycles;
}

void write_all(RTL8735B::RCOutput &output, uint16_t a, uint16_t b, uint16_t c, uint16_t d)
{
    output.cork();
    output.write(0, a);
    output.write(1, b);
    output.write(2, c);
    output.write(3, d);
    output.push();
}

// 切成 DShot：四路接上 GPIO 送幀，不再走 PWM；push 一次送一幀，各路同時
void test_dshot_mode_and_frames()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_freq(0x0FU, 400);
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    uint32_t mask = 0;
    assert(output.get_output_mode(mask) == AP_HAL::RCOutput::MODE_PWM_DSHOT300 && mask == 0x0FU);
    assert(dshot_pins.size() == 4);
    for (uint8_t ch = 0; ch < 4; ch++) {
        assert(dshot_pins[ch] == rtl8735b_rcout_pin(ch));
    }
    assert(fake_scheduler.has_timer_process());

    // 未解鎖：各路送數值 0；脈寬 0 的那一路不送
    write_all(output, 1000, 1500, 2000, 0);
    assert(dshot_sends.size() == 1);
    assert(dshot_sends[0].active == 0x7U);
    for (uint8_t ch = 0; ch < 3; ch++) {
        assert(dshot_sends[0].frames[ch] == rtl8735b_dshot_frame(0, 0));
    }
    assert(same_timing(dshot_sends[0].timing, timing_for(300, 0)));
    // 沒有走 PWM
    assert(apply_count == 0 && apply_ticks_count == 0 && oneshot125_count == 0);

    // 解鎖後依脈寬換成油門
    fake_util.soft_armed = true;
    write_all(output, 1000, 1500, 2000, 1100);
    assert(dshot_sends.size() == 2);
    assert(dshot_sends[1].active == 0xFU);
    assert(dshot_sends[1].frames[0] == rtl8735b_dshot_frame(0, 0));
    assert(dshot_sends[1].frames[1] == rtl8735b_dshot_frame(1048, 0));
    assert(dshot_sends[1].frames[2] == rtl8735b_dshot_frame(2047, 0));
    assert(dshot_sends[1].frames[3] == rtl8735b_dshot_frame(248, 0));
    assert(output.read(1) == 1500);

    // 沒 cork 的 write 立即送
    output.write(1, 1200);
    assert(dshot_sends.size() == 3);
    assert(dshot_sends[2].frames[1] == rtl8735b_dshot_frame(448, 0));
    assert(apply_count == 0);
}

// 遙測請求：只在下一幀帶，送出後清掉；那一路沒有送幀時留到它送的時候
void test_dshot_telemetry_request()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT600);
    fake_util.soft_armed = true;
    output.set_telem_request_mask(1U << 1);
    write_all(output, 1100, 1100, 1100, 1100);
    const uint16_t plain = rtl8735b_dshot_frame(248, 0);
    const uint16_t telem = rtl8735b_dshot_frame(248, 1);
    assert(dshot_sends.back().frames[0] == plain);
    assert(dshot_sends.back().frames[1] == telem);
    assert(dshot_sends.back().frames[2] == plain);
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.back().frames[1] == plain);

    output.set_telem_request_mask(1U << 3);
    write_all(output, 1100, 1100, 1100, 0);
    assert((dshot_sends.back().active & (1U << 3)) == 0);
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.back().frames[3] == telem);
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.back().frames[3] == plain);
}

// 安全開關：關著時不送任何幀（不是送 0）；主程式沒 push 時由計時器續送，太久才補
void test_dshot_safety_and_keepalive()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    fake_util.soft_armed = true;
    output.force_safety_on();
    write_all(output, 1500, 1500, 1500, 1500);
    fake_scheduler.run_timer();
    assert(dshot_sends.empty());

    output.force_safety_off();
    now_us = 10000;
    write_all(output, 1500, 1500, 1500, 1500);
    assert(dshot_sends.size() == 1);
    now_us = 11000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == 1);
    now_us = 14500;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == 2);
    assert(memcmp(dshot_sends[1].frames, dshot_sends[0].frames, sizeof(dshot_sends[0].frames)) == 0);
    assert(dshot_sends[1].active == dshot_sends[0].active);
    now_us = 15000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == 2);

    // 號誌被別的執行緒拿著時，計時器不等、不送
    std::mutex m;
    std::condition_variable cv;
    bool held = false, release = false;
    std::thread holder([&] {
        semaphore_mutex.lock();
        {
            std::lock_guard<std::mutex> g(m);
            held = true;
        }
        cv.notify_all();
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return release; });
        semaphore_mutex.unlock();
    });
    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return held; });
    }
    now_us = 30000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == 2);
    {
        std::lock_guard<std::mutex> g(m);
        release = true;
    }
    cv.notify_all();
    holder.join();

    // 安全開關關上之後，計時器也不送
    output.force_safety_on();
    now_us = 60000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == 2);
}

// 解鎖中主迴圈停住、不再 push：計時器補送在主迴圈停住不久時照舊用最後的油門；停住太久就改送 0（停轉），
// 不再把停住前的油門一直送給電調。命令照舊；主迴圈恢復後照常送油門
void test_dshot_keepalive_stops_motors_when_main_loop_stalls()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    fake_util.soft_armed = true;
    const uint16_t run = rtl8735b_dshot_frame(rtl8735b_dshot_throttle_value(1500, 1), 0);
    const uint16_t stop = rtl8735b_dshot_frame(0, 0);
    now_us = 10000;
    write_all(output, 1500, 1500, 1500, 1500);
    assert(dshot_sends.size() == 1 && dshot_sends[0].frames[0] == run);

    // 主迴圈停住不久（50 ms）：補送照舊
    main_loop_ms_ago = 50;
    now_us += 5000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == 2);
    for (uint8_t ch = 0; ch < 4; ch++) {
        assert(dshot_sends.back().frames[ch] == run);
    }

    // 停住太久（250 ms）：補送改成 0，之後一直送 0
    main_loop_ms_ago = 250;
    for (int i = 0; i < 3; i++) {
        now_us += 5000;
        fake_scheduler.run_timer();
        assert(dshot_sends.back().active == dshot_sends[0].active);
        for (uint8_t ch = 0; ch < 4; ch++) {
            assert(dshot_sends.back().frames[ch] == stop);
        }
    }
    assert(dshot_sends.size() == 5);

    // 命令照舊：停住期間排進的命令照樣送（命令通道送命令，其他送 0）
    output.send_dshot_command(5, 1, 0, 10, true);
    now_us += 1000;
    fake_scheduler.run_timer();
    assert(dshot_sends.back().frames[1] == rtl8735b_dshot_frame(5, 1));
    assert(dshot_sends.back().frames[0] == rtl8735b_dshot_frame(0, 0));
    for (int i = 0; i < 9; i++) {
        now_us += 1000;
        fake_scheduler.run_timer();
    }

    // 主迴圈恢復：push 照常送油門
    main_loop_ms_ago = 0;
    write_all(output, 1500, 1500, 1500, 1500);
    for (uint8_t ch = 0; ch < 4; ch++) {
        assert(dshot_sends.back().frames[ch] == run);
    }
    now_us += 5000;
    fake_scheduler.run_timer();
    assert(dshot_sends.back().frames[0] == run);
}

// 解鎖中主迴圈停住超過 100 ms：所有 PWM 通道停止輸出（一般 PWM 停用、有刷以 duty 0 拉低），只停一次；
// 未解鎖時不動；主迴圈恢復後第一次 push 恢復輸出
void test_pwm_stops_when_main_loop_stalls()
{
    reset_fake_outputs();
    reset_dshot();
    {
        RTL8735B::RCOutput output;
        output.init();
        output.force_safety_off();
        for (uint8_t chan = 0; chan < 4; chan++) {
            output.enable_ch(chan);
        }
        fake_util.soft_armed = true;
        write_all(output, 1500, 1500, 1500, 1500);
        for (uint8_t chan = 0; chan < 4; chan++) {
            assert(output_active[chan] && output_pulse[chan] == 1500);
        }
        // 停住不久（50 ms）：照舊輸出
        main_loop_ms_ago = 50;
        fake_scheduler.run_timer();
        for (uint8_t chan = 0; chan < 4; chan++) {
            assert(output_active[chan]);
        }
        // 停住太久（250 ms）：全部停止，之後不重複停
        main_loop_ms_ago = 250;
        fake_scheduler.run_timer();
        for (uint8_t chan = 0; chan < 4; chan++) {
            assert(!output_active[chan]);
        }
        const unsigned stops = stop_count;
        fake_scheduler.run_timer();
        assert(stop_count == stops);
        // 主迴圈恢復：第一次 push 恢復全部通道，包括這次沒寫到的
        main_loop_ms_ago = 0;
        output.cork();
        output.write(0, 1600);
        output.push();
        assert(output_active[0] && output_pulse[0] == 1600);
        for (uint8_t chan = 1; chan < 4; chan++) {
            assert(output_active[chan] && output_pulse[chan] == 1500);
        }
        // 未解鎖時主迴圈停住：不動（未解鎖的輸出由 AP_Motors 自己壓到最低）
        fake_util.soft_armed = false;
        main_loop_ms_ago = 250;
        fake_scheduler.run_timer();
        assert(output_active[0] && output_pulse[0] == 1600);
    }

    // 有刷馬達：停止是 duty 0 拉低，馬達不再照最後的 duty 轉
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput brushed;
    brushed.init();
    brushed.force_safety_off();
    setup_brushed(brushed, 0x0FU, 16000);
    for (uint8_t chan = 0; chan < 4; chan++) {
        brushed.enable_ch(chan);
    }
    brushed.set_esc_scaling(1000, 2000);
    fake_util.soft_armed = true;
    write_all(brushed, 1500, 1500, 1500, 1500);
    assert(output_is_ticks[0] && output_duty_ticks[0] != 0);
    main_loop_ms_ago = 250;
    fake_scheduler.run_timer();
    for (uint8_t chan = 0; chan < 4; chan++) {
        assert(output_low_driven[chan] && output_duty_ticks[chan] == 0);
    }
}

// 電調型別決定位寬；速率改成別的 DShot 時所有 DShot 通道一起改；DShot 通道改回 PWM 要重開機：
// 停送並說明，不碰 PWM
void test_dshot_esc_type_rate_and_switching()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    output.set_dshot_esc_type(AP_HAL::RCOutput::DSHOT_ESC_BLHELI_S);
    assert(output.get_dshot_esc_type() == AP_HAL::RCOutput::DSHOT_ESC_BLHELI_S);
    output.set_dshot_rate(2, 400);
    write_all(output, 1000, 1000, 1000, 1000);
    assert(same_timing(dshot_sends.back().timing, timing_for(300, 1)));

    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_DSHOT600);
    write_all(output, 1000, 1000, 1000, 1000);
    assert(same_timing(dshot_sends.back().timing, timing_for(600, 1)));
    assert(dshot_sends.back().active == 0xFU);
    uint32_t mask = 0;
    assert(output.get_output_mode(mask) == AP_HAL::RCOutput::MODE_PWM_DSHOT600 && mask == 0x0FU);

    fake_statustext().clear();
    output.set_output_mode(1U << 2, AP_HAL::RCOutput::MODE_PWM_NORMAL);
    write_all(output, 1000, 1000, 1500, 1000);
    assert(dshot_sends.back().active == 0xBU);
    assert(apply_count == 0 && !output_active[2]);
    assert(fake_statustext().find("reboot") != std::string::npos);
    assert(output.get_output_mode(mask) == AP_HAL::RCOutput::MODE_PWM_DSHOT600 && mask == 0x0BU);
    // 同一路只說一次
    fake_statustext().clear();
    output.set_output_mode(1U << 2, AP_HAL::RCOutput::MODE_PWM_NORMAL);
    assert(fake_statustext().empty());
}

// 接腳失敗的那一路停用並說明原因，其他路照送
void test_dshot_attach_failure()
{
    reset_fake_outputs();
    reset_dshot();
    dshot_fail_pin = rtl8735b_rcout_pin(2);
    dshot_fail_code = RTL8735B_DSHOT_EGPIO;
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    uint32_t mask = 0;
    assert(output.get_output_mode(mask) == AP_HAL::RCOutput::MODE_PWM_DSHOT300 && mask == 0x0BU);
    assert(fake_statustext().find("ch3") != std::string::npos);
    write_all(output, 1000, 1000, 1000, 1000);
    assert(dshot_sends.size() == 1);
    // 槽位照接上的順序：ch1、ch2、ch4
    assert(dshot_sends[0].active == 0x7U);

    // 校正失敗時那一路也不能用
    reset_fake_outputs();
    reset_dshot();
    dshot_fail_pin = rtl8735b_rcout_pin(0);
    dshot_fail_code = RTL8735B_DSHOT_ECLOCK;
    RTL8735B::RCOutput output2;
    output2.init();
    output2.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_DSHOT150);
    assert(output2.get_output_mode(mask) == AP_HAL::RCOutput::MODE_PWM_NONE && mask == 0);
    assert(fake_statustext().find("cycle counter") != std::string::npos);
}

// 送幀回報已緊急停止：之後不再送
void test_dshot_stopped()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    dshot_stopped = true;
    write_all(output, 1000, 1000, 1000, 1000);
    assert(dshot_sends.size() == 1);
    write_all(output, 1000, 1000, 1000, 1000);
    now_us += 100000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == 1);
}

// 一幀送到一半 CPU 週期計數器出錯（ETIMING）：這一幀作廢，遙測請求留到下一幀；主控台與地面站只提示一次；
// DShot 不停，之後照常送
void test_dshot_timing_loss()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    fake_util.soft_armed = true;
    const uint16_t plain = rtl8735b_dshot_frame(248, 0);
    const uint16_t telem = rtl8735b_dshot_frame(248, 1);
    fake_statustext().clear();
    output.set_telem_request_mask(1U << 1);
    dshot_send_result = RTL8735B_DSHOT_ETIMING;
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.back().frames[1] == telem);
    assert(fake_statustext().find("DShot frame dropped") != std::string::npos);
    // 作廢的那一幀不算送出：遙測請求留到下一幀
    dshot_send_result = RTL8735B_DSHOT_OK;
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.back().frames[1] == telem);
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.back().frames[1] == plain);
    // 再作廢一次：不再提示，也不停止 DShot
    fake_statustext().clear();
    const size_t before = dshot_sends.size();
    dshot_send_result = RTL8735B_DSHOT_ETIMING;
    write_all(output, 1100, 1100, 1100, 1100);
    assert(fake_statustext().empty());
    dshot_send_result = RTL8735B_DSHOT_OK;
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.size() == before + 2);
}

// DShot 命令（嗶聲、反轉、3D 等）：取代該路的油門幀並帶遙測位元，其他 DShot 通道送 0；至少送 10 次，
// 有逾時就照每毫秒一幀算；解鎖後只收 priority 命令；排隊依序送
void test_dshot_commands()
{
    reset_fake_outputs();
    reset_dshot();
    RTL8735B::RCOutput output;
    output.init();
    output.force_safety_off();
    output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
    const uint16_t zero = rtl8735b_dshot_frame(0, 0);

    // 全部通道；還沒寫過脈寬的通道也送命令
    output.send_dshot_command(1, AP_HAL::RCOutput::ALL_CHANNELS, 0, 10, false);
    for (int i = 0; i < 10; i++) {
        write_all(output, 1000, 1000, 1000, 0);
        assert(dshot_sends.back().active == 0xFU);
        for (uint8_t ch = 0; ch < 4; ch++) {
            assert(dshot_sends.back().frames[ch] == rtl8735b_dshot_frame(1, 1));
        }
    }
    write_all(output, 1000, 1000, 1000, 0);
    assert(dshot_sends.back().active == 0x7U);
    assert(dshot_sends.back().frames[0] == zero);

    // 單一通道：其他通道送 0；次數取 repeat 與 10 的大者
    output.send_dshot_command(21, 2, 0, 12, false);
    for (int i = 0; i < 12; i++) {
        write_all(output, 1000, 1000, 1000, 1000);
        assert(dshot_sends.back().active == 0xFU);
        assert(dshot_sends.back().frames[2] == rtl8735b_dshot_frame(21, 1));
        assert(dshot_sends.back().frames[0] == zero && dshot_sends.back().frames[3] == zero);
    }
    write_all(output, 1000, 1000, 1000, 1000);
    assert(dshot_sends.back().frames[2] == zero);

    // 排隊：先送完第一個再送第二個
    output.send_dshot_command(2, 0, 0, 10, false);
    output.send_dshot_command(3, 1, 0, 10, false);
    for (int i = 0; i < 10; i++) {
        write_all(output, 1000, 1000, 1000, 1000);
        assert(dshot_sends.back().frames[0] == rtl8735b_dshot_frame(2, 1));
    }
    for (int i = 0; i < 10; i++) {
        write_all(output, 1000, 1000, 1000, 1000);
        assert(dshot_sends.back().frames[1] == rtl8735b_dshot_frame(3, 1));
    }
    write_all(output, 1000, 1000, 1000, 1000);
    assert(dshot_sends.back().frames[1] == zero);

    // 逾時 30 ms：30 幀；命令等著送時，計時器每 1 ms 就補一幀
    output.send_dshot_command(4, 3, 30, 10, false);
    now_us = 200000;
    write_all(output, 1000, 1000, 1000, 1000);
    const size_t start = dshot_sends.size();
    for (int i = 1; i < 30; i++) {
        now_us += 1000;
        fake_scheduler.run_timer();
    }
    assert(dshot_sends.size() == start + 29);
    assert(dshot_sends.back().frames[3] == rtl8735b_dshot_frame(4, 1));
    now_us += 1000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == start + 29);
    write_all(output, 1000, 1000, 1000, 1000);
    assert(dshot_sends.back().frames[3] == zero);

    // 解鎖後只收 priority 命令；不是 DShot 的通道不收
    fake_util.soft_armed = true;
    output.send_dshot_command(5, 0, 0, 10, false);
    write_all(output, 1000, 1000, 1000, 1000);
    assert(dshot_sends.back().frames[0] == zero);
    output.send_dshot_command(5, 0, 0, 10, true);
    write_all(output, 1000, 1000, 1000, 1000);
    assert(dshot_sends.back().frames[0] == rtl8735b_dshot_frame(5, 1));
    for (int i = 0; i < 9; i++) {
        write_all(output, 1000, 1000, 1000, 1000);
    }
    output.send_dshot_command(6, 9, 0, 10, true);
    write_all(output, 1100, 1100, 1100, 1100);
    assert(dshot_sends.back().frames[0] == rtl8735b_dshot_frame(248, 0));

    // 安全開關關著時命令也不送
    output.force_safety_on();
    const size_t before = dshot_sends.size();
    output.send_dshot_command(1, AP_HAL::RCOutput::ALL_CHANNELS, 0, 10, true);
    now_us += 5000;
    fake_scheduler.run_timer();
    assert(dshot_sends.size() == before);
}

// 開機橫幅：連續同模式的通道合成一段，NONE 不列
void test_output_mode_banner()
{
    reset_fake_outputs();
    reset_dshot();
    char banner[64];
    {
        RTL8735B::RCOutput output;
        output.init();
        assert(output.get_output_mode_banner(banner, sizeof(banner)));
        assert(strcmp(banner, "RCOut: PWM:1-4") == 0);
    }
    {
        dshot_fail_pin = rtl8735b_rcout_pin(3);
        dshot_fail_code = RTL8735B_DSHOT_EGPIO;
        RTL8735B::RCOutput output;
        output.init();
        output.set_output_mode(0x03U, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
        output.set_output_mode(1U << 3, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
        assert(output.get_output_mode_banner(banner, sizeof(banner)));
        assert(strcmp(banner, "RCOut: DS300:1-2 PWM:3") == 0);
    }
    {
        reset_dshot();
        RTL8735B::RCOutput output;
        output.init();
        output.set_output_mode(0x0FU, AP_HAL::RCOutput::MODE_PWM_DSHOT1200);
        assert(output.get_output_mode_banner(banner, sizeof(banner)));
        assert(strcmp(banner, "RCOut: None") == 0);
    }
}
}

int main()
{
    test_board_pin_profile_and_whitelist();
    test_rc_output_startup_and_safe_transition();
    test_frequency_change_drops_corked_write();
    test_invalid_rate_pulse_and_mode_fail_closed();
    test_rc_output_safety_calls_under_thread_contention();
    test_normal_and_brushed_rate_policy();
    test_init_does_not_touch_pins();
    test_brushed_duty_mapping();
    test_brushed_mode_outputs_duty_and_zero_at_min();
    test_brushed_safety_forces_zero_and_is_not_replayed();
    test_brushed_rejects_unsupported_rate_fail_closed();
    test_brushed_rate_reported_only_when_write_rejected();
    test_set_default_rate_keeps_motor_channels();
    test_normal_and_oneshot_paths_unchanged();
    test_push_flushes_deferred_pwm_writes();
    test_oneshot125_mapping();
    test_oneshot125_mode_outputs_scaled_pulse();
    test_oneshot125_clock_conflict_is_reported_once();
    test_brushed_clock_conflict_is_reported_once();
    test_brushed_to_normal_switch_holds_low_first();
    test_oneshot125_to_brushed_switch_stops_old_output();
    test_oneshot_mode_uses_normal_pwm_path();
    test_dshot_mode_and_frames();
    test_dshot_telemetry_request();
    test_dshot_safety_and_keepalive();
    test_dshot_keepalive_stops_motors_when_main_loop_stalls();
    test_pwm_stops_when_main_loop_stalls();
    test_dshot_esc_type_rate_and_switching();
    test_dshot_attach_failure();
    test_dshot_stopped();
    test_dshot_timing_loss();
    test_dshot_commands();
    test_output_mode_banner();
    puts("IO contract host tests passed");
    return 0;
}
