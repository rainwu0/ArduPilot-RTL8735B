#include "IOConfig.h"
#include "RCOutput.h"
#include "io_shim.h"

#include <assert.h>
#include <mutex>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <thread>
#include <unistd.h>

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

    output.set_output_mode(1U << 2, AP_HAL::RCOutput::MODE_PWM_DSHOT300);
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

    // DShot 不支援：停止輸出並回報為 NONE。
    output.write(0, 1500);
    assert(output_active[0]);
    output.set_output_mode(1U << 0, AP_HAL::RCOutput::MODE_PWM_DSHOT600);
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

// 改輸出模式時，要先照舊模式停止輸出再換模式：新模式可能因為 PWM 時脈被另一種模式占用而驅動不了腳位，
// 這時舊的輸出不能留著。safety 也要讓通道真的停止，而不是只把軟體的值歸零。
// （同一次開機在 HAL 層直接換模式才會遇到；Copter 的 MOT_PWM_TYPE 要重開機才生效。）
static void test_mode_change_stops_old_output_when_new_mode_cannot_drive()
{
    reset_fake_outputs();
    RTL8735B::RCOutput output;
    output.init();
    const uint8_t ch = 2;
    output.set_output_mode(1U << ch, AP_HAL::RCOutput::MODE_PWM_ONESHOT125);
    output.enable_ch(ch);
    output.write(ch, 1250);
    assert(output_active[ch] && output_is_oneshot125[ch]);

    apply_ticks_result = RTL8735B_PWM_ERR_CLOCK;   // 有刷的寫入一律因時脈衝突被拒
    StdoutCapture capture;
    stop_events.clear();
    output.set_output_mode(1U << ch, AP_HAL::RCOutput::MODE_PWM_BRUSHED);
    assert(stop_events.find('S') != std::string::npos && !output_active[ch]);
    output.write(ch, 1700);
    assert(!output_active[ch]);
    output_active[ch] = true;   // 假設通道又被別的路徑啟動
    assert(output.force_safety_on());
    (void)capture.finish();
    assert(!output_active[ch] && output.read(ch) == 0);
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
    test_mode_change_stops_old_output_when_new_mode_cannot_drive();
    test_brushed_to_normal_switch_holds_low_first();
    test_oneshot_mode_uses_normal_pwm_path();
    puts("IO contract host tests passed");
    return 0;
}
