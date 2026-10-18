#include "AnalogIn.h"
#include "GPIO.h"
#include "IOConfig.h"
#include "io_shim.h"
#include "io_fakes/AP_HAL/AP_HAL.h"
#include "io_fakes/PinNames.h"
#include "io_fakes/gpio_irq_api.h"
#include "io_fakes/hal_pwm.h"
#include "io_fakes/io_shim_sdk_fake.h"

#include <assert.h>
#include <math.h>
#include <mutex>
#include <stdint.h>
#include <string.h>

namespace {
std::recursive_mutex semaphore_mutex;
AP_HAL::Scheduler scheduler;
int irq_a_count;
int irq_b_count;
uint8_t irq_pin;
bool irq_state;
uint32_t irq_time;

void irq_a(uint8_t pin, bool state, uint32_t timestamp)
{
    irq_a_count++;
    irq_pin = pin;
    irq_state = state;
    irq_time = timestamp;
}

void irq_b(uint8_t pin, bool state, uint32_t timestamp)
{
    irq_b_count++;
    irq_pin = pin;
    irq_state = state;
    irq_time = timestamp;
}

void shim_irq(uint32_t, uint8_t, uint32_t)
{}
}

const AP_HAL::HAL fake_hal(&scheduler);
const AP_HAL::HAL &hal = fake_hal;

namespace RTL8735B {
Semaphore::Semaphore() : _handle(nullptr) {}
bool Semaphore::give() { semaphore_mutex.unlock(); return true; }
bool Semaphore::take(uint32_t) { semaphore_mutex.lock(); return true; }
bool Semaphore::take_nonblocking() { return semaphore_mutex.try_lock(); }
void Semaphore::take_blocking() { semaphore_mutex.lock(); }
bool Semaphore::check_owner() { return true; }
}

extern "C" uint32_t rtl8735b_irq_save_disable(void)
{
    return 0;
}

extern "C" void rtl8735b_irq_restore(uint32_t)
{}

static void test_gpio_whitelist_and_output_order()
{
    RTL8735B::GPIO gpio;
    assert(gpio.valid_pin(7));
    assert(gpio.valid_pin(25));
    assert(gpio.valid_pin(26));
    assert(!gpio.valid_pin(2));
    assert(gpio.channel(7) != nullptr);
    assert(gpio.channel(2) == nullptr);

    const unsigned init_before = io_fake_gpio_init_calls;
    gpio.pinMode(7, HAL_GPIO_OUTPUT);
    assert(io_fake_gpio_init_calls == init_before);
    gpio.pinMode(7, HAL_GPIO_INPUT);
    assert(io_fake_gpio_init_calls == init_before + 1);
    // 輸入腳不輸出：write 不碰 SDK
    const unsigned writes_before = io_fake_gpio_write_calls;
    gpio.write(7, 1);
    assert(io_fake_gpio_write_calls == writes_before);

    gpio.pinMode(25, HAL_GPIO_OUTPUT);
    assert(io_fake_last_gpio_order_write < io_fake_last_gpio_order_dir);
    gpio.write(25, 1);
    assert(gpio.read(25) == 1);
    gpio.toggle(25);
    assert(gpio.read(25) == 0);
}

static void test_gpio_irq_replacement_detach_and_invalid_request()
{
    RTL8735B::GPIO gpio;
    assert(gpio.attach_interrupt(7, irq_a, AP_HAL::GPIO::INTERRUPT_RISING));
    const unsigned init_calls = io_fake_gpio_irq_init_calls;
    const unsigned deinit_calls = io_fake_gpio_irq_deinit_calls;

    io_fake_fire_irq(0, IRQ_RISE);
    assert(irq_a_count == 1 && irq_b_count == 0);
    assert(irq_pin == 7 && irq_state && irq_time == io_fake_timestamp_us);

    assert(!gpio.attach_interrupt(2, irq_b, AP_HAL::GPIO::INTERRUPT_RISING));
    assert(!gpio.attach_interrupt(7, irq_b, AP_HAL::GPIO::INTERRUPT_BOTH));
    assert(io_fake_gpio_irq_init_calls == init_calls);
    assert(io_fake_gpio_irq_deinit_calls == deinit_calls);
    io_fake_fire_irq(0, IRQ_RISE);
    assert(irq_a_count == 2 && irq_b_count == 0);

    assert(gpio.attach_interrupt(7, irq_b, AP_HAL::GPIO::INTERRUPT_FALLING));
    assert(io_fake_gpio_irq_deinit_calls == deinit_calls + 1);
    io_fake_fire_irq(0, IRQ_FALL);
    assert(irq_a_count == 2 && irq_b_count == 1);
    assert(irq_pin == 7 && !irq_state);

    assert(gpio.attach_interrupt(7, (AP_HAL::Proc)nullptr, AP_HAL::GPIO::INTERRUPT_NONE));
    const unsigned stopped_deinit_calls = io_fake_gpio_irq_deinit_calls;
    io_fake_fire_irq(0, IRQ_RISE);
    assert(irq_b_count == 1);
    assert(stopped_deinit_calls == deinit_calls + 2);

    // io_shim 本身也只接受下降緣（1）與上升緣（2），不靠 GPIO.cpp 先擋
    const unsigned shim_init_calls = io_fake_gpio_irq_init_calls;
    assert(rtl8735b_gpio_irq_attach(7, 0, 0, shim_irq) == -1);
    assert(rtl8735b_gpio_irq_attach(7, 3, 0, shim_irq) == -1);
    assert(io_fake_gpio_irq_init_calls == shim_init_calls);
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-3f;
}

// 12 位元、滿刻度 3.3 V 的線性換算（維護者 2026-10-15 確認滿刻度）；偏移由上層參數（例如 BATT_VLT_OFFSET）修正
static void test_adc_init_failure_raw_sample_and_voltage()
{
    io_fake_adc_init_status = 7;
    RTL8735B::AnalogIn init_failed;
    init_failed.init();
    assert(init_failed.channel(2) == nullptr);

    io_fake_adc_init_status = 0;
    io_fake_adc_cvlist_status = 9;
    RTL8735B::AnalogIn list_failed;
    list_failed.init();
    assert(list_failed.channel(2) == nullptr);

    io_fake_adc_cvlist_status = 0;
    RTL8735B::AnalogIn analog;
    analog.init();
    assert(analog.valid_analog_pin(2));
    assert(!analog.valid_analog_pin(1));
    assert(analog.channel(1) == nullptr);
    assert(analog.channel(ANALOG_INPUT_BOARD_VCC) == nullptr);
    AP_HAL::AnalogSource *source = analog.channel(2);
    assert(source != nullptr);
    // 還沒有取樣：原始值與電壓都是 NaN
    assert(isnan(source->read_latest()));
    assert(isnan(source->voltage_latest()));
    assert(isnan(source->voltage_average()));
    assert(isnan(source->voltage_average_ratiometric()));
    assert(isnan(analog.board_voltage()));

    // 只接受板型 hwdef 列出的類比腳
    for (uint8_t pin = 0; pin < 8; pin++) {
        assert(source->set_pin(pin) == (pin == 2));
    }
    assert(!source->set_pin(ANALOG_INPUT_BOARD_VCC));

    io_fake_adc_values[0] = 2048;
    scheduler.service_io();
    assert(source->read_latest() == 2048.0f);
    assert(near(source->voltage_latest(), 2048.0f * 3.3f / 4095.0f));
    // 沒有 5 V 監測：ratiometric 與一般電壓相同（比照 AP_HAL_ESP32）
    assert(near(source->voltage_average_ratiometric(), 2048.0f * 3.3f / 4095.0f));

    io_fake_adc_values[0] = 4095;
    scheduler.service_io();
    assert(near(source->voltage_latest(), 3.3f));
    io_fake_adc_values[0] = 0;
    scheduler.service_io();
    assert(near(source->voltage_latest(), 0.0f));
    assert(near(source->voltage_average(), (4095.0f + 0.0f) / 2.0f * 3.3f / 4095.0f));

    // AP_BattMonitor_Analog::read() 每次讀電壓前都呼叫 set_pin(同一支腳)（ArduPilot 9f648ccabc
    // AP_BattMonitor_Analog.cpp:157-160）；腳位沒變時保留累計，比照 AP_HAL_ChibiOS AnalogIn.cpp:228
    io_fake_adc_values[0] = 2048;
    scheduler.service_io();
    assert(source->set_pin(2));
    assert(near(source->voltage_average(), 2048.0f * 3.3f / 4095.0f));
    scheduler.service_io();
    assert(source->set_pin(2));
    assert(near(source->voltage_latest(), 2048.0f * 3.3f / 4095.0f));
    assert(near(source->voltage_average(), 2048.0f * 3.3f / 4095.0f));

    // 預設的表：PF0 是 ADC 通道 0
    assert(io_fake_adc_pin_en == 1U && io_fake_adc_cvlist_channel == 0 && io_fake_adc_read_channel == 0);
    assert(rtl8735b_adc_read_u16(1) == -1);
    assert(io_fake_adc_init_calls == 3);
}

static void test_pwm_shim_rejects_bad_values_and_stops_sdk_init_default()
{
    assert(io_fake_pwm_init_calls == 0);
    assert(rtl8735b_pwm_apply(4, 20000, 1500) != 0);
    assert(rtl8735b_pwm_apply(0, 20000, 20000) != 0);
    assert(io_fake_pwm_init_calls == 0);

    assert(rtl8735b_pwm_apply(0, 20000, 1500) == 0);
    assert(io_fake_pwm_init_calls == 1);
    assert(io_fake_pwm_stop_calls == 1);
    assert(io_fake_pwm_start_calls == 1);
    // 週期與脈寬以整數一次寫入（hal_pwm_set_duty），寫入後才啟用。
    assert(io_fake_pwm_duty_us_calls == 1);
    assert(io_fake_order_pwm_init < io_fake_order_pwm_stop);
    assert(io_fake_order_pwm_stop < io_fake_order_pwm_duty_us);
    assert(io_fake_order_pwm_duty_us < io_fake_order_pwm_start);
    assert(io_fake_pwm_running[0]);
    assert(io_fake_pwm_period[0] == 20000);
    assert(io_fake_pwm_pulse[0] == 1500);

    rtl8735b_pwm_stop(0);
    assert(!io_fake_pwm_running[0]);
    assert(io_fake_pwm_pulse[0] == 0);
    // mbed 的 pwmout_period_us／pwmout_pulsewidth_us 以 float 換算（約 5.6% 的脈寬少 1 us），
    // 而且每次寫入呼叫兩次 ROM，一律不用。
    assert(io_fake_pwm_period_calls == 0);
    assert(io_fake_pwm_pulse_calls == 0);
}

// 一般 PWM 的寫入：每次只呼叫一次 ROM；值沒變不寫；通道啟用中而上一次的設定還沒被硬體套用
// （CTRL_SET 未清除）時不呼叫 ROM（ROM 會忙等到週期結束），記下最新值，由 rtl8735b_pwm_flush
// （RCOutput::push 每次呼叫）在套用後寫入。開始時通道 0、3 已停用（前面的測試留下）。
static void test_pwm_normal_write_once_and_defer_while_pending()
{
    const unsigned calls = io_fake_pwm_duty_us_calls;
    const unsigned waits = io_fake_pwm_duty_us_waits;
    assert(rtl8735b_pwm_apply(0, 2040, 1500) == 0);
    assert(io_fake_pwm_duty_us_calls == calls + 1);
    assert(io_fake_pwm_running[0]);
    assert(io_fake_pwm_period[0] == 2040 && io_fake_pwm_pulse[0] == 1500);

    // 值沒變：不呼叫 ROM（不論 CTRL_SET）。
    assert(rtl8735b_pwm_apply(0, 2040, 1500) == 0);
    io_fake_pwm_period_end(0);
    assert(rtl8735b_pwm_apply(0, 2040, 1500) == 0);
    assert(io_fake_pwm_duty_us_calls == calls + 1);

    // 已套用：新值立即寫入。
    assert(rtl8735b_pwm_apply(0, 2040, 1600) == 0);
    assert(io_fake_pwm_duty_us_calls == calls + 2);
    assert(io_fake_pwm_pulse[0] == 1600);

    // 還沒套用：延後，最新的值優先；flush 在套用前不寫。
    assert(rtl8735b_pwm_apply(0, 2040, 1700) == 0);
    assert(rtl8735b_pwm_apply(0, 2040, 1800) == 0);
    rtl8735b_pwm_flush();
    assert(io_fake_pwm_duty_us_calls == calls + 2);
    assert(io_fake_pwm_pulse[0] == 1600);
    io_fake_pwm_period_end(0);
    rtl8735b_pwm_flush();
    assert(io_fake_pwm_duty_us_calls == calls + 3);
    assert(io_fake_pwm_pulse[0] == 1800);
    rtl8735b_pwm_flush();
    assert(io_fake_pwm_duty_us_calls == calls + 3);

    // 延後期間又寫回已寫入的值：取消延後的值，不在之後寫入舊命令。
    assert(rtl8735b_pwm_apply(0, 2040, 1900) == 0);
    assert(rtl8735b_pwm_apply(0, 2040, 1800) == 0);
    io_fake_pwm_period_end(0);
    rtl8735b_pwm_flush();
    assert(io_fake_pwm_duty_us_calls == calls + 3);
    assert(io_fake_pwm_pulse[0] == 1800);

    // 週期改變也是一次寫入。
    assert(rtl8735b_pwm_apply(0, 2500, 1500) == 0);
    assert(io_fake_pwm_duty_us_calls == calls + 4);
    assert(io_fake_pwm_period[0] == 2500 && io_fake_pwm_pulse[0] == 1500);
    // 一般寫入從不等待 CTRL_SET。
    assert(io_fake_pwm_duty_us_waits == waits);

    // 停止會丟掉延後的值：之後 flush 不得重新輸出。
    assert(rtl8735b_pwm_apply(0, 2500, 1600) == 0);
    rtl8735b_pwm_stop(0);
    assert(!io_fake_pwm_running[0] && io_fake_pwm_pulse[0] == 0);
    io_fake_pwm_period_end(0);
    rtl8735b_pwm_flush();
    assert(!io_fake_pwm_running[0] && io_fake_pwm_pulse[0] == 0);

    // 停用中的通道直接寫入（硬體在停用時不清 CTRL_SET，不能等它），寫入後才啟用。
    assert(rtl8735b_pwm_apply(3, 2040, 1200) == 0);
    assert(io_fake_pwm_running[3] && io_fake_pwm_pulse[3] == 1200);
    rtl8735b_pwm_stop(3);
    assert(!io_fake_pwm_running[3]);

    // ROM 回報錯誤：輸出失敗並停止。
    io_fake_pwm_duty_us_status = 6;
    assert(rtl8735b_pwm_apply(0, 2040, 1500) != 0);
    assert(!io_fake_pwm_running[0]);
    io_fake_pwm_duty_us_status = 0;
    assert(io_fake_pwm_period_calls == 0);
    assert(io_fake_pwm_pulse_calls == 0);
}

static void test_pwm_shim_brushed_ticks_use_sclk_duty_ns_path()
{
    io_fake_reset();
    // 參數檢查失敗時不得碰 SDK。duty 必須小於週期（DUTY 等於週期的行為沒有文件）。
    assert(rtl8735b_pwm_apply_ticks(4, 2500, 1250) != 0);
    assert(rtl8735b_pwm_apply_ticks(1, 0, 0) != 0);
    assert(rtl8735b_pwm_apply_ticks(1, 4096, 100) != 0);
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 2500) != 0);
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 2501) != 0);
    assert(io_fake_pwm_init_calls == 0);
    assert(io_fake_pwm_duty_ns_calls == 0);

    // 第一次使用（duty 0）：初始化後先停掉 SDK 預設的 20 ms 輸出，再以 25 ns tick 寫入週期與
    // DUTY 0，最後啟用；之後腳位由 PWM 以 duty 0 持續驅動。
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 0) == 0);
    assert(io_fake_pwm_init_calls == 1);
    assert(io_fake_pwm_stop_calls == 1);
    assert(io_fake_pwm_duty_ns_calls == 1);
    assert(io_fake_pwm_enable_calls == 1);
    assert(io_fake_order_pwm_init < io_fake_order_pwm_stop);
    assert(io_fake_order_pwm_stop < io_fake_order_pwm_duty_ns);
    assert(io_fake_order_pwm_duty_ns < io_fake_order_pwm_enable);
    assert(io_fake_pwm_period_ns[1] == 62500U);
    assert(io_fake_pwm_duty_ns[1] == 0U);
    assert(io_fake_pwm_offset_ns[1] == 0U);
    assert(io_fake_pwm_running[1]);
    // 不經 mbed 的微秒 API（其週期與脈寬只有微秒解析度）。
    assert(io_fake_pwm_period_calls == 0);
    assert(io_fake_pwm_pulse_calls == 0);

    // 執行中改 duty：只更新暫存器，不重新啟用。
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 1250) == 0);
    assert(io_fake_pwm_duty_ns_calls == 2);
    assert(io_fake_pwm_enable_calls == 1);
    assert(io_fake_pwm_duty_ns[1] == 31250U);
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 2499) == 0);
    assert(io_fake_pwm_duty_ns[1] == 62475U);

    // 停止：DUTY 寫 0、保持啟用，不停用通道、不經微秒 API。
    rtl8735b_pwm_stop(1);
    assert(io_fake_pwm_duty_ns_calls == 4);
    assert(io_fake_pwm_duty_ns[1] == 0U);
    assert(io_fake_pwm_period_ns[1] == 62500U);
    assert(io_fake_pwm_disable_calls == 0);
    assert(io_fake_pwm_running[1]);
    assert(io_fake_pwm_pulse_calls == 0);

    // 已切到 SCLK tick 的通道不能再用微秒 API 輸出（需重新開機），必須回報失敗且不碰 SDK。
    const unsigned duty_calls = io_fake_pwm_duty_ns_calls;
    assert(rtl8735b_pwm_apply(1, 20000, 1500) != 0);
    assert(io_fake_pwm_period_calls == 0);
    assert(io_fake_pwm_pulse_calls == 0);
    assert(io_fake_pwm_duty_ns_calls == duty_calls);
    assert(io_fake_pwm_duty_ns[1] == 0U);

    // SDK 回報錯誤：輸出失敗；DUTY 0 也寫不進時停用通道，不留在舊的 duty。
    io_fake_pwm_duty_ns_status = 6;
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 100) != 0);
    assert(io_fake_pwm_duty_ns_calls == duty_calls + 2);
    assert(io_fake_pwm_disable_calls == 1);
    assert(!io_fake_pwm_running[1]);
    io_fake_pwm_duty_ns_status = 0;
    // 恢復後重新啟用。
    assert(rtl8735b_pwm_apply_ticks(1, 4000, 4000 - 1) == 0);
    assert(io_fake_pwm_period_ns[1] == 100000U);
    assert(io_fake_pwm_duty_ns[1] == 99975U);
    assert(io_fake_pwm_running[1]);
    assert(io_fake_pwm_enable_calls == 2);
    // 已停用的通道收到停止時，寫入 DUTY 0 後重新啟用，讓腳位回到由 PWM 驅動的 duty 0。
    io_fake_pwm_duty_ns_status = 6;
    rtl8735b_pwm_stop(1);
    assert(!io_fake_pwm_running[1]);
    io_fake_pwm_duty_ns_status = 0;
    rtl8735b_pwm_stop(1);
    assert(io_fake_pwm_running[1]);
    assert(io_fake_pwm_duty_ns[1] == 0U);
    assert(io_fake_pwm_enable_calls == 3);

    // PWM SCLK 已是 40 MHz（有刷）：OneShot125 被拒，而且不碰 SDK（通道 2 維持未初始化）。
    const unsigned inits_before_oneshot = io_fake_pwm_init_calls;
    assert(rtl8735b_pwm_apply_oneshot125(2, 500) == RTL8735B_PWM_ERR_CLOCK);
    assert(io_fake_pwm_init_calls == inits_before_oneshot);
    assert(io_fake_pwm_sclk_sel_calls == 0);

    // 一般 PWM 通道仍走原本的微秒路徑：脈寬 0 後以 mbed 停用，不呼叫 hal_pwm_*。
    const unsigned disable_calls = io_fake_pwm_disable_calls;
    const unsigned duty_calls_before_normal = io_fake_pwm_duty_ns_calls;
    assert(rtl8735b_pwm_apply(3, 2040, 1500) == 0);
    assert(io_fake_pwm_period[3] == 2040);
    assert(io_fake_pwm_pulse[3] == 1500);
    rtl8735b_pwm_stop(3);
    assert(!io_fake_pwm_running[3]);
    assert(io_fake_pwm_pulse[3] == 0);
    assert(io_fake_pwm_disable_calls == disable_calls);
    assert(io_fake_pwm_duty_ns_calls == duty_calls_before_normal);
}

// AP_HAL::panic 第一步呼叫的緊急停止。旗標在本次執行內不會解除，所以這個測試必須排在最後。
// 開始時的狀態（前面的測試留下）：通道 0、3 走一般 PWM 已初始化，通道 1 已改用 SCLK tick，
// 通道 2 從未使用。替身的狀態接著前一個測試，不重設，免得與 io_shim 內部的執行狀態不一致。
static void test_pwm_emergency_stop_zeroes_all_and_latches()
{
    const unsigned init_calls = io_fake_pwm_init_calls;
    // 三路輸出中：兩路一般 PWM、一路有刷（SCLK tick）。
    assert(rtl8735b_pwm_apply(0, 2500, 1500) == 0);
    assert(rtl8735b_pwm_apply(3, 2040, 1200) == 0);
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 1250) == 0);
    assert(io_fake_pwm_running[0] && io_fake_pwm_pulse[0] == 1500);
    assert(io_fake_pwm_running[3] && io_fake_pwm_pulse[3] == 1200);
    assert(io_fake_pwm_running[1] && io_fake_pwm_duty_ns[1] == 31250U);
    assert(io_fake_pwm_init_calls == init_calls);
    // 通道 0 留一個延後中的非零值：緊急停止之後不得被 flush 寫出。
    assert(rtl8735b_pwm_apply(0, 2500, 1900) == 0);
    assert(rtl8735b_pwm_apply(0, 2500, 1950) == 0);
    assert(io_fake_pwm_pulse[0] == 1900);

    // 有刷通道正在寫入非零 duty 時被 panic 搶先：緊急停止先歸零四路，之後這筆寫入才落到
    // 暫存器。寫入方回來後必須看到旗標、再歸零並回報失敗。
    io_fake_pwm_duty_ns_preempt = rtl8735b_pwm_emergency_stop;
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 2000) != 0);
    assert(io_fake_pwm_duty_ns_preempt == nullptr);
    assert(io_fake_pwm_duty_ns[1] == 0U);
    assert(io_fake_pwm_period_ns[1] == 62500U);
    assert(io_fake_pwm_running[1]); // 有刷：保持啟用、DUTY 0，不停用通道
    // 一般 PWM：脈寬 0 後停用。
    assert(io_fake_pwm_pulse[0] == 0 && !io_fake_pwm_running[0]);
    assert(io_fake_pwm_pulse[3] == 0 && !io_fake_pwm_running[3]);
    // 從未使用的通道不初始化，維持晶片預設狀態。
    assert(io_fake_pwm_init_calls == init_calls);
    assert(!io_fake_pwm_running[2]);

    // 之後的非零輸出一律拒絕，而且不碰 SDK。
    const unsigned duty_us_calls = io_fake_pwm_duty_us_calls;
    const unsigned start_calls = io_fake_pwm_start_calls;
    const unsigned duty_calls = io_fake_pwm_duty_ns_calls;
    const unsigned enable_calls = io_fake_pwm_enable_calls;
    assert(rtl8735b_pwm_apply(0, 2500, 1500) != 0);
    assert(rtl8735b_pwm_apply(3, 2040, 1000) != 0);
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 1) != 0);
    assert(rtl8735b_pwm_apply(2, 20000, 1500) != 0);
    assert(rtl8735b_pwm_apply_ticks(2, 2500, 1250) != 0);
    io_fake_pwm_period_end(0);
    rtl8735b_pwm_flush();
    assert(io_fake_pwm_duty_us_calls == duty_us_calls);
    assert(io_fake_pwm_start_calls == start_calls);
    assert(io_fake_pwm_duty_ns_calls == duty_calls);
    assert(io_fake_pwm_enable_calls == enable_calls);
    assert(io_fake_pwm_init_calls == init_calls);
    for (unsigned i = 0; i < 4; i++) {
        assert(io_fake_pwm_pulse[i] == 0);
        assert(io_fake_pwm_duty_ns[i] == 0U);
    }

    // 歸零仍然允許：有刷的 duty 0 維持由 PWM 驅動低電位，停止照常。
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 0) == 0);
    assert(io_fake_pwm_duty_ns[1] == 0U && io_fake_pwm_running[1]);
    rtl8735b_pwm_stop(0);
    assert(io_fake_pwm_pulse[0] == 0 && !io_fake_pwm_running[0]);

    // 再呼叫一次緊急停止（例如 panic 重入）不出錯，結果相同。
    rtl8735b_pwm_emergency_stop();
    assert(io_fake_pwm_duty_ns[1] == 0U && io_fake_pwm_running[1]);
    assert(io_fake_pwm_init_calls == init_calls);
}

static uint32_t pwm_duty_field(int index)
{
    return io_fake_pwm_regs(index)->PWM_CTRL & PWM_MASK_DUTY;
}

static bool pwm_ctrl_set(int index)
{
    return (io_fake_pwm_regs(index)->PWM_CTRL & PWM_BIT_CTRL_SET) != 0U;
}

// OneShot125：PWM SCLK 改成 4 MHz，週期與 duty 以 tick 寫入暫存器。有刷會把 SCLK 鎖在 40 MHz，
// 所以這個情境在另一個行程執行（run.sh 以參數 oneshot125 呼叫）。
static int oneshot125_scenario()
{
    io_fake_reset();
    // 參數不合法：不碰 SDK。
    assert(rtl8735b_pwm_apply_oneshot125(4, 500) != 0);
    assert(rtl8735b_pwm_apply_oneshot125(0, 0) != 0);
    assert(rtl8735b_pwm_apply_oneshot125(0, RTL8735B_ONESHOT125_PERIOD_TICKS) != 0);
    assert(io_fake_pwm_init_calls == 0 && io_fake_pwm_sclk_sel_calls == 0);

    // 第一次使用：pwmout_init（它第一次會把 SCLK 設成 40 MHz）之後才改成 4 MHz；
    // 通道選 SCLK、PERIOD 欄位寫 tick 數減一（與 SDK hal_pwm_set_duty_ns 相同）、DUTY 寫 tick 數，
    // 寫完才啟用（停用中寫入不設 CTRL_SET，硬體在啟用時套用）。
    assert(rtl8735b_pwm_apply_oneshot125(0, 750) == 0);
    assert(io_fake_pwm_init_calls == 1 && io_fake_pwm_stop_calls == 1);
    assert(io_fake_pwm_sclk_sel_calls == 1 && io_fake_pwm_sclk == PWM_Sclk_4M);
    assert(io_fake_order_pwm_init < io_fake_order_pwm_sclk_sel);
    assert(io_fake_order_pwm_sclk_sel < io_fake_order_pwm_enable);
    assert(io_fake_pwm_clk_sel[0] == 8);
    assert((io_fake_pwm_regs(0)->PWM_TIMING_CTRL & PWM_MASK_PERIOD) == RTL8735B_ONESHOT125_PERIOD_TICKS - 1U);
    assert((io_fake_pwm_regs(0)->PWM_TIMING_CTRL & PWM_MASK_DUTY_START) == 0U);
    assert(pwm_duty_field(0) == 750U && !pwm_ctrl_set(0));
    assert(io_fake_pwm_running[0]);
    assert(io_fake_pwm_duty_us_calls == 0 && io_fake_pwm_duty_ns_calls == 0);

    // 第二個通道不再改時鐘。
    assert(rtl8735b_pwm_apply_oneshot125(3, 500) == 0);
    assert(io_fake_pwm_sclk_sel_calls == 1 && io_fake_pwm_running[3] && pwm_duty_field(3) == 500U);

    // 值沒變不寫；新值寫入並設 CTRL_SET；還沒套用時延後，由 flush 在套用後寫入。
    const unsigned updates = io_fake_pwm_update_ctrl_calls;
    assert(rtl8735b_pwm_apply_oneshot125(0, 750) == 0);
    assert(io_fake_pwm_update_ctrl_calls == updates);
    assert(rtl8735b_pwm_apply_oneshot125(0, 800) == 0);
    assert(pwm_duty_field(0) == 800U && pwm_ctrl_set(0));
    assert(io_fake_pwm_update_ctrl_calls == updates + 1);
    assert(rtl8735b_pwm_apply_oneshot125(0, 900) == 0);
    assert(rtl8735b_pwm_apply_oneshot125(0, 950) == 0);
    rtl8735b_pwm_flush();
    assert(pwm_duty_field(0) == 800U);
    io_fake_pwm_period_end(0);
    rtl8735b_pwm_flush();
    assert(pwm_duty_field(0) == 950U && io_fake_pwm_update_ctrl_calls == updates + 2);
    assert(io_fake_pwm_wait_ready_calls == 0);

    // 有刷被拒（時鐘），不碰 SDK；OneShot125 通道不能改走微秒路徑；其他通道的一般 PWM 照常。
    const unsigned inits = io_fake_pwm_init_calls;
    assert(rtl8735b_pwm_apply_ticks(1, 2500, 1250) == RTL8735B_PWM_ERR_CLOCK);
    assert(io_fake_pwm_init_calls == inits && io_fake_pwm_duty_ns_calls == 0);
    assert(rtl8735b_pwm_apply(0, 2040, 1500) != 0);
    assert(pwm_duty_field(0) == 950U);
    assert(rtl8735b_pwm_apply(2, 2040, 1500) == 0);
    assert(io_fake_pwm_running[2] && io_fake_pwm_pulse[2] == 1500);

    // 停止不延後：等上一次的設定套用後把 DUTY 寫 0，通道保持啟用（與有刷相同）。
    assert(pwm_ctrl_set(0));
    rtl8735b_pwm_stop(0);
    assert(io_fake_pwm_wait_ready_calls == 1);
    assert(pwm_duty_field(0) == 0U && io_fake_pwm_running[0]);
    // 停止後再輸出。
    io_fake_pwm_period_end(0);
    assert(rtl8735b_pwm_apply_oneshot125(0, 600) == 0);
    assert(pwm_duty_field(0) == 600U);

    // 緊急停止：延後中的值丟掉，全部歸零；之後拒絕非零輸出。
    io_fake_pwm_period_end(3);
    assert(rtl8735b_pwm_apply_oneshot125(3, 600) == 0);
    assert(rtl8735b_pwm_apply_oneshot125(3, 700) == 0);
    assert(pwm_duty_field(3) == 600U);
    rtl8735b_pwm_emergency_stop();
    assert(pwm_duty_field(0) == 0U && pwm_duty_field(3) == 0U);
    assert(io_fake_pwm_running[0] && io_fake_pwm_running[3]);
    assert(io_fake_pwm_pulse[2] == 0 && !io_fake_pwm_running[2]);
    io_fake_pwm_period_end(3);
    rtl8735b_pwm_flush();
    assert(pwm_duty_field(3) == 0U);
    assert(rtl8735b_pwm_apply_oneshot125(3, 500) != 0);
    assert(pwm_duty_field(3) == 0U);
    return 0;
}

#ifdef IO_ALT_BOARD
// 非預設的表（io_fakes/hwdef.h 的 IO_ALT_BOARD）：GPIO 17、23、24，類比 PF3（ADC 通道 3、邏輯編號 4），
// 5 路 PWM。HAL 的腳位、編號與數量都要跟著表走，不能有寫死的預設值。
static int alt_board_scenario()
{
    io_fake_reset();
    assert(rtl8735b_rcout_channel_count() == 5);
    assert(rtl8735b_rcout_pin(4) == PIN_F15);
    assert(rtl8735b_rcout_pin(5) == RTL8735B_INVALID_PIN);

    RTL8735B::GPIO gpio;
    assert(gpio.valid_pin(17) && gpio.valid_pin(23) && gpio.valid_pin(24));
    assert(!gpio.valid_pin(7) && !gpio.valid_pin(25) && !gpio.valid_pin(26));
    assert(!rtl8735b_gpio_output_allowed(17));
    assert(rtl8735b_gpio_output_allowed(23) && rtl8735b_gpio_output_allowed(24));
    // 中斷回呼收到的是表裡的邏輯編號（第 3 列是 24）
    assert(gpio.attach_interrupt(24, irq_a, AP_HAL::GPIO::INTERRUPT_RISING));
    io_fake_fire_irq(2, IRQ_RISE);
    assert(irq_a_count == 1 && irq_pin == 24);

    // 類比：PF3 是 ADC 通道 3，PA2、PA3 是外接通道 6、7。初始化打開表裡每一個通道，轉換清單用第一條；
    // 每支腳各自讀自己的通道、各自累計，互不覆蓋
    RTL8735B::AnalogIn analog;
    analog.init();
    assert(analog.valid_analog_pin(4) && analog.valid_analog_pin(9) && analog.valid_analog_pin(10));
    assert(!analog.valid_analog_pin(2));
    assert(io_fake_adc_pin_en == ((1U << 3) | (1U << 6) | (1U << 7)) && io_fake_adc_cvlist_channel == 3);
    assert(io_fake_adc_init_calls == 1);
    AP_HAL::AnalogSource *source = analog.channel(4);
    AP_HAL::AnalogSource *current = analog.channel(9);
    AP_HAL::AnalogSource *other = analog.channel(10);
    assert(source != nullptr && current != nullptr && other != nullptr);
    io_fake_adc_values[3] = 1234;
    io_fake_adc_values[6] = 300;
    io_fake_adc_values[7] = 4000;
    scheduler.service_io();
    assert(source->read_latest() == 1234.0f);
    assert(current->read_latest() == 300.0f && other->read_latest() == 4000.0f);
    io_fake_adc_values[6] = 500;
    scheduler.service_io();
    assert(current->read_average() == 400.0f && source->read_average() == 1234.0f);
    // set_pin 換到另一支腳：之後讀新腳的通道
    assert(current->set_pin(10));
    scheduler.service_io();
    assert(current->read_latest() == 4000.0f);
    return 0;
}
#endif

int main(int argc, char **argv)
{
#ifdef IO_ALT_BOARD
    (void)argc;
    (void)argv;
    return alt_board_scenario();
#endif
    if (argc > 1 && strcmp(argv[1], "oneshot125") == 0) {
        return oneshot125_scenario();
    }
    io_fake_reset();
    test_gpio_whitelist_and_output_order();
    test_gpio_irq_replacement_detach_and_invalid_request();
    test_adc_init_failure_raw_sample_and_voltage();
    test_pwm_shim_rejects_bad_values_and_stops_sdk_init_default();
    test_pwm_shim_brushed_ticks_use_sclk_duty_ns_path();
    test_pwm_normal_write_once_and_defer_while_pending();
    test_pwm_emergency_stop_zeroes_all_and_latches();
    return 0;
}
