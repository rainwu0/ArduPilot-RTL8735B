/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "io_shim.h"
#include "IOConfig.h"

#include <PinNames.h>
#include <gpio_irq_api.h>
#include <gpio_irq_ex_api.h>
#include <gpio_api.h>
#include <hal_adc.h>
#include <hal_pwm.h>
#include <hal_timer.h>
#include <objects.h>
#include <pwmout_api.h>
#include <pwmout_ex_api.h>
#include <rtl8735b_adc.h>
#include <string.h>
#include <us_ticker_api.h>

static gpio_t gpio_objects[RTL8735B_GPIO_PIN_COUNT];
static uint8_t gpio_initialized[RTL8735B_GPIO_PIN_COUNT];
static uint8_t gpio_is_output[RTL8735B_GPIO_PIN_COUNT];
static gpio_irq_t gpio_irq_objects[RTL8735B_GPIO_PIN_COUNT];
static rtl8735b_gpio_irq_handler gpio_irq_handlers[RTL8735B_GPIO_PIN_COUNT];
static uint8_t gpio_irq_active[RTL8735B_GPIO_PIN_COUNT];

// 本 HAL 獨占 ADC adapter；mbed analogin_init() 無回傳值且隱藏 adapter/init 狀態。
static hal_adc_adapter_t adc_adapter;
static uint8_t adc_initialized;
static uint8_t adc_channel;

static pwmout_t pwm_objects[RTL8735B_RCOUT_CHANNELS];
static uint8_t pwm_initialized[RTL8735B_RCOUT_CHANNELS];
static uint8_t pwm_running[RTL8735B_RCOUT_CHANNELS];
// 1 表示此通道已改用 40 MHz SCLK tick（hal_pwm_set_duty_ns，有刷）；本次開機內不再走微秒路徑（hal_pwm_set_duty）。
static uint8_t pwm_sclk_mode[RTL8735B_RCOUT_CHANNELS];
static uint16_t pwm_sclk_period_ticks[RTL8735B_RCOUT_CHANNELS];
// PWM SCLK 的頻率是全域的（有刷用 40 MHz、OneShot125 用 4 MHz）：第一次用到時決定，本次開機內不再改。
#define PWM_SCLK_UNSET 0U
#define PWM_SCLK_40MHZ 1U
#define PWM_SCLK_4MHZ 2U
static uint8_t pwm_sclk_clock;
// PWM_CTRL 的 CLK_SEL：8 為 SCLK。依據是 SDK rtl8735b_pwm_type.h 的欄位說明，以及 Realtek
// Ameba-AIoT/ameba-rtos-pro2@eb5c0907c40d 的 component/soc/8735b/fwlib/rtl8735b/source/ram/hal_pwm.c:348
// （hal_pwm_set_duty_ns 以 8 選 SCLK；有刷已在板上用這個值）。SDK 列舉 PwmClkSrc_SClk 卻是 16，
// OneShot125 的通道未上板確認。
#define PWM_CLK_SEL_SCLK 8U
// 1 表示此通道以 4 MHz SCLK 輸出 OneShot125；最後寫入的 duty tick。
static uint8_t pwm_oneshot125[RTL8735B_RCOUT_CHANNELS];
static uint16_t pwm_oneshot125_duty[RTL8735B_RCOUT_CHANNELS];
// 一般 PWM：最後寫入硬體的週期與脈寬，以及硬體還沒套用上一次設定時延後的值
// （一般 PWM 延後週期與脈寬，OneShot125 延後 duty tick）。
static uint16_t pwm_period_us[RTL8735B_RCOUT_CHANNELS];
static uint16_t pwm_pulse_us[RTL8735B_RCOUT_CHANNELS];
static uint8_t pwm_deferred[RTL8735B_RCOUT_CHANNELS];
static uint16_t pwm_deferred_period_us[RTL8735B_RCOUT_CHANNELS];
static uint16_t pwm_deferred_pulse_us[RTL8735B_RCOUT_CHANNELS];
static uint16_t pwm_deferred_duty_ticks[RTL8735B_RCOUT_CHANNELS];
// 1 表示已緊急停止（rtl8735b_pwm_emergency_stop）：本次開機內只接受脈寬／duty 0。
// 放在 RCOutput 的 mutex 之下、不依賴 FreeRTOS，之後任何執行緒的非零寫入都擋得住。
static volatile uint8_t pwm_emergency_stopped;

static int gpio_slot(uint8_t logical_pin)
{
    for (uint8_t i = 0; i < RTL8735B_GPIO_PIN_COUNT; i++) {
        if (rtl8735b_gpio_logical_pin(i) == logical_pin &&
            rtl8735b_gpio_pin(logical_pin) != RTL8735B_INVALID_PIN) {
            return (int)i;
        }
    }
    return -1;
}

int rtl8735b_gpio_mode(uint8_t logical_pin, uint8_t output)
{
    const int slot = gpio_slot(logical_pin);
    const uint32_t sdk_pin = rtl8735b_gpio_pin(logical_pin);
    if (slot < 0 || sdk_pin == RTL8735B_INVALID_PIN) {
        return -1;
    }
    if (output && !rtl8735b_gpio_output_allowed(logical_pin)) {
        return -1;
    }

    gpio_t *obj = &gpio_objects[slot];
    if (!gpio_initialized[slot]) {
        memset(obj, 0, sizeof(*obj));
        gpio_init(obj, (PinName)sdk_pin);
        gpio_initialized[slot] = 1;
    }
    gpio_mode(obj, PullNone);
    if (output) {
        // 先預載關閉值，再切方向，避免腳位短暫被拉高。
        gpio_write(obj, 0);
        gpio_dir(obj, PIN_OUTPUT);
    } else {
        gpio_dir(obj, PIN_INPUT);
    }
    gpio_is_output[slot] = output != 0;
    return 0;
}

int rtl8735b_gpio_read(uint8_t logical_pin)
{
    const int slot = gpio_slot(logical_pin);
    if (slot < 0 || !gpio_initialized[slot]) {
        return -1;
    }
    return gpio_read(&gpio_objects[slot]) != 0;
}

int rtl8735b_gpio_write(uint8_t logical_pin, uint8_t value)
{
    const int slot = gpio_slot(logical_pin);
    if (slot < 0 || !gpio_initialized[slot] || !gpio_is_output[slot]) {
        return -1;
    }
    gpio_write(&gpio_objects[slot], value != 0);
    return 0;
}

static void gpio_irq_trampoline(uint32_t irq_id, gpio_irq_event event)
{
    if (irq_id >= RTL8735B_GPIO_PIN_COUNT || gpio_irq_handlers[irq_id] == NULL) {
        return;
    }
    const uint8_t value = event == IRQ_RISE;
    const uint32_t timestamp_us = us_ticker_read();
    gpio_irq_handlers[irq_id](irq_id, value, timestamp_us);
}

void rtl8735b_gpio_irq_detach(uint8_t logical_pin)
{
    const int slot = gpio_slot(logical_pin);
    if (slot < 0 || !gpio_irq_active[slot]) {
        return;
    }
    gpio_irq_disable(&gpio_irq_objects[slot]);
    gpio_irq_handlers[slot] = NULL;
    gpio_irq_deinit(&gpio_irq_objects[slot]);
    gpio_irq_active[slot] = 0;
}

int rtl8735b_gpio_irq_attach(uint8_t logical_pin, uint8_t trigger, uint32_t irq_id,
                              rtl8735b_gpio_irq_handler handler)
{
    const int slot = gpio_slot(logical_pin);
    if (slot < 0 || irq_id != (uint32_t)slot || handler == NULL || trigger < 1 || trigger > 2) {
        return -1;
    }
    rtl8735b_gpio_irq_detach(logical_pin);
    const gpio_irq_event event = trigger == 1 ? IRQ_FALL : IRQ_RISE;
    const int status = gpio_irq_init(&gpio_irq_objects[slot], (PinName)rtl8735b_gpio_pin(logical_pin),
                                     gpio_irq_trampoline, irq_id);
    if (status != 0) {
        return status;
    }
    gpio_irq_handlers[slot] = handler;
    gpio_irq_active[slot] = 1;
    gpio_irq_set(&gpio_irq_objects[slot], event, 1);
    return 0;
}

int rtl8735b_adc_init(uint8_t logical_pin)
{
    // ADC 通道來自開發板 hwdef 的 RTL8735B_ADC 表：PF0–PF3 是通道 0–3，PA0–PA3 是外接通道 4–7
    // （SDK component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_adc.h 的 HP_ADC_CH_NO、HP_ADC_EXCH_NO，
    // component/mbed/targets/hal/rtl8735b/analogin_api.c 的腳位表）
    const uint8_t channel = rtl8735b_adc_channel(logical_pin);
    if (rtl8735b_analog_pin(logical_pin) == RTL8735B_INVALID_PIN || channel > 7U) {
        return -1;
    }
    if (!adc_initialized) {
        memset(&adc_adapter, 0, sizeof(adc_adapter));
        hal_adc_load_default(&adc_adapter);
        // 表裡每一支類比腳都在初始化時打開；轉換清單只放 logical_pin 那一個，單次讀取指定通道
        // （和 SDK mbed 的 analogin_read 相同，analogin_api.c 以 hal_adc_single_read 讀各自的通道）
        for (uint8_t i = 0; i < rtl8735b_adc_channel_count(); i++) {
            const uint8_t ch = rtl8735b_adc_channel(rtl8735b_adc_logical_pin(i));
            if (ch <= 7U) {
                adc_adapter.plft_dat.pin_en.w |= 1UL << ch;
            }
        }
        hal_status_t status = hal_adc_init(&adc_adapter);
        if (status != HAL_OK) {
            return (int)status;
        }
        hal_delay_ms(20);
        hal_adc_set_in_type_all(&adc_adapter, HP_ADC_INPUT_ALL_SINGLE);
        adc_channel = channel;
        status = hal_adc_set_cvlist(&adc_adapter, &adc_channel, 1);
        if (status != HAL_OK) {
            hal_adc_deinit(&adc_adapter);
            memset(&adc_adapter, 0, sizeof(adc_adapter));
            return (int)status;
        }
        adc_initialized = 1;
    }
    return 0;
}

int32_t rtl8735b_adc_read_u16(uint8_t logical_pin)
{
    const uint8_t channel = rtl8735b_adc_channel(logical_pin);
    if (rtl8735b_analog_pin(logical_pin) == RTL8735B_INVALID_PIN || channel > 7U || !adc_initialized) {
        return -1;
    }
    // 與 SDK analogin_read_u16 相同，回傳 12 位元原始碼；讀取 API 不提供失敗狀態。
    return (int32_t)hal_adc_single_read(&adc_adapter, channel);
}

static int pwm_ensure_initialized(uint8_t channel)
{
    pwmout_t *obj = &pwm_objects[channel];
    if (!pwm_initialized[channel]) {
        memset(obj, 0, sizeof(*obj));
        pwmout_init(obj, (PinName)rtl8735b_rcout_pin(channel));
        if (!obj->is_init) {
            return -1;
        }
        // pwmout_init 會啟動 20 ms 通道，預設脈衝為零；先停掉，再設定明確授權的週期與脈寬。
        pwmout_stop(obj);
        pwm_initialized[channel] = 1;
        pwm_running[channel] = 0;
        pwm_period_us[channel] = 20000U;
        pwm_pulse_us[channel] = 0U;
    }
    return 0;
}

// CTRL_SET：軟體寫入新設定後設為 1，硬體在週期結束套用後清除；通道停用中不清除。
// 為 1 時呼叫 hal_pwm_set_duty 會在 ROM 裡忙等到週期結束（推論，依 SDK 的暫存器說明；Realtek
// ameba-rtos-pro2@eb5c0907c40d 的 hal_pwm.c:355，hal_pwm_set_duty_ns 在通道啟用中先等 ctrl ready）。
static int pwm_ctrl_pending(uint8_t channel)
{
    const PWM_TypeDef *regs = pwm_objects[channel].pwm_hal_adp.base_addr;
    return pwm_running[channel] && (regs->PWM_CTRL & PWM_BIT_CTRL_SET) != 0U;
}

// 以整數一次寫入週期與脈寬。不用 mbed 的 pwmout_period_us／pwmout_pulsewidth_us：
// 它們以 float 換算（部分脈寬少 1 us），而且每次寫入呼叫兩次 ROM。
static int pwm_write_us(uint8_t channel, uint16_t period_us, uint16_t pulse_us)
{
    pwmout_t *obj = &pwm_objects[channel];
    pwm_deferred[channel] = 0;
    if (hal_pwm_set_duty(&obj->pwm_hal_adp, period_us, pulse_us, 0U) != HAL_OK) {
        rtl8735b_pwm_stop(channel);
        return -1;
    }
    pwm_period_us[channel] = period_us;
    pwm_pulse_us[channel] = pulse_us;
    if (!pwm_running[channel]) {
        pwmout_start(obj);
        pwm_running[channel] = 1;
    }
    return 0;
}

// OneShot125 以暫存器寫入週期與 duty（4 MHz SCLK，hal_pwm_set_duty_ns 只接受 40 MHz）。
// 寫法照 Realtek Ameba-AIoT/ameba-rtos-pro2@eb5c0907c40d 的
// component/soc/8735b/fwlib/rtl8735b/source/ram/hal_pwm.c:333-380（hal_pwm_set_duty_ns）：
// PERIOD 欄位寫 tick 數減一（:357；沒有文件，未上板量測），DUTY 寫 tick 數；
// 通道啟用中先等 ctrl ready（:355）才設 CTRL_SET，停用中由硬體在啟用時套用。呼叫前上一次的設定必須已套用。
static int pwm_write_oneshot125(uint8_t channel, uint16_t duty_ticks)
{
    hal_pwm_adapter_t *adp = &pwm_objects[channel].pwm_hal_adp;
    pwm_deferred[channel] = 0;
    hal_pwm_set_period(adp, RTL8735B_ONESHOT125_PERIOD_TICKS - 1U);
    hal_pwm_set_onduty_start(adp, 0U);
    hal_pwm_set_duty_size(adp, duty_ticks);
    pwm_oneshot125_duty[channel] = duty_ticks;
    if (pwm_running[channel]) {
        hal_pwm_update_ctrl(adp);
    } else {
        hal_pwm_enable(adp);
        pwm_running[channel] = 1;
    }
    return 0;
}

int rtl8735b_pwm_apply(uint8_t channel, uint16_t period_us, uint16_t pulse_us)
{
    if (channel >= RTL8735B_RCOUT_CHANNELS || period_us == 0 || pulse_us >= period_us ||
        rtl8735b_rcout_pin(channel) == RTL8735B_INVALID_PIN) {
        return -1;
    }
    // 有刷與 OneShot125 已把此通道的 tick 來源改成 SCLK；微秒路徑的
    // hal_pwm_set_duty 是否會改回 G-Timer tick 未查證，因此不混用（MOT_PWM_TYPE 本來就需重開機）。
    if (pwm_sclk_mode[channel] || pwm_oneshot125[channel]) {
        return -1;
    }
    if (pwm_emergency_stopped && pulse_us != 0U) {
        return -1;
    }
    if (pwm_ensure_initialized(channel) != 0) {
        return -1;
    }

    if (pwm_running[channel] && period_us == pwm_period_us[channel] &&
        pulse_us == pwm_pulse_us[channel]) {
        // 值沒變就不寫；延後中的值也作廢（寫回了已寫入的值）。
        pwm_deferred[channel] = 0;
        return 0;
    }
    if (pwm_ctrl_pending(channel)) {
        // 上一次的設定要到這個週期結束才套用。ESC 每個週期只取樣一次，不必等：
        // 記下最新的值，由 rtl8735b_pwm_flush 在套用後寫入。
        pwm_deferred[channel] = 1;
        pwm_deferred_period_us[channel] = period_us;
        pwm_deferred_pulse_us[channel] = pulse_us;
        return 0;
    }
    if (pwm_write_us(channel, period_us, pulse_us) != 0) {
        return -1;
    }
    if (pwm_emergency_stopped && pulse_us != 0U) {
        // 寫入途中被緊急停止搶先：緊急停止的歸零可能早於上面的寫入，這裡再歸零一次。
        rtl8735b_pwm_stop(channel);
        return -1;
    }
    return 0;
}

void rtl8735b_pwm_flush(void)
{
    for (uint8_t channel = 0; channel < RTL8735B_RCOUT_CHANNELS; channel++) {
        if (!pwm_deferred[channel] || pwm_ctrl_pending(channel)) {
            continue;
        }
        if (pwm_emergency_stopped) {
            pwm_deferred[channel] = 0;
            continue;
        }
        const int status = pwm_oneshot125[channel] ?
            pwm_write_oneshot125(channel, pwm_deferred_duty_ticks[channel]) :
            pwm_write_us(channel, pwm_deferred_period_us[channel], pwm_deferred_pulse_us[channel]);
        if (status == 0 && pwm_emergency_stopped) {
            // 同 rtl8735b_pwm_apply：寫入途中被緊急停止搶先時再歸零一次。
            rtl8735b_pwm_stop(channel);
        }
    }
}

int rtl8735b_pwm_apply_ticks(uint8_t channel, uint16_t period_ticks, uint16_t duty_ticks)
{
    if (channel >= RTL8735B_RCOUT_CHANNELS || period_ticks == 0U ||
        period_ticks > RTL8735B_PWM_MAX_TICKS || duty_ticks >= period_ticks ||
        rtl8735b_rcout_pin(channel) == RTL8735B_INVALID_PIN) {
        return -1;
    }
    if (pwm_sclk_clock == PWM_SCLK_4MHZ) {
        return RTL8735B_PWM_ERR_CLOCK;
    }
    if (pwm_emergency_stopped && duty_ticks != 0U) {
        return -1;
    }
    if (pwm_ensure_initialized(channel) != 0) {
        return -1;
    }

    pwmout_t *obj = &pwm_objects[channel];
    pwm_sclk_clock = PWM_SCLK_40MHZ;
    pwm_sclk_mode[channel] = 1;
    pwm_sclk_period_ticks[channel] = period_ticks;
    // hal_pwm_set_duty_ns 要求 PWM SCLK 為 40 MHz（pwmout_init 在 clk_sel 預設 0 時設定），
    // 把 tick 來源設為 SCLK，週期與 duty 以 25 ns 為單位寫入；通道執行中時先等 CTRL_SET 清除，
    // 新值在週期結束時生效。
    const hal_status_t status = hal_pwm_set_duty_ns(&obj->pwm_hal_adp,
        (uint32_t)period_ticks * RTL8735B_PWM_SCLK_TICK_NS,
        (uint32_t)duty_ticks * RTL8735B_PWM_SCLK_TICK_NS, 0U);
    if (status != HAL_OK) {
        rtl8735b_pwm_stop(channel);
        return -1;
    }
    if (!pwm_running[channel]) {
        hal_pwm_enable(&obj->pwm_hal_adp);
        pwm_running[channel] = 1;
    }
    if (pwm_emergency_stopped && duty_ticks != 0U) {
        // 同 rtl8735b_pwm_apply：寫入途中被緊急停止搶先時再歸零一次。
        rtl8735b_pwm_stop(channel);
        return -1;
    }
    return 0;
}

void rtl8735b_pwm_emergency_stop(void)
{
    // 先設旗標再歸零：之後的非零寫入一律拒絕；寫入途中被搶先的執行緒回來後
    // 會看到旗標並自行歸零（見 rtl8735b_pwm_apply／rtl8735b_pwm_apply_ticks）。
    pwm_emergency_stopped = 1;
    for (uint8_t channel = 0; channel < RTL8735B_RCOUT_CHANNELS; channel++) {
        // 沒初始化過的通道 HAL 從未驅動，維持晶片預設狀態，這裡不呼叫 pwmout_init。
        rtl8735b_pwm_stop(channel);
    }
}

void rtl8735b_pwm_stop(uint8_t channel)
{
    if (channel >= RTL8735B_RCOUT_CHANNELS || !pwm_initialized[channel]) {
        return;
    }
    if (pwm_sclk_mode[channel]) {
        // 有刷通道的停止狀態是「啟用中、DUTY 為 0」，讓腳位持續由 PWM 驅動，而不是停用後
        // 落到未查證的輸出準位。
        // 推論：DUTY 為 0 時輸出持續低電位；SDK 文件沒有寫明，需上板確認。
        pwmout_t *obj = &pwm_objects[channel];
        const hal_status_t status = hal_pwm_set_duty_ns(&obj->pwm_hal_adp,
            (uint32_t)pwm_sclk_period_ticks[channel] * RTL8735B_PWM_SCLK_TICK_NS, 0U, 0U);
        if (status != HAL_OK) {
            // 寫不進 DUTY 0 時，不能讓通道留在舊的 duty 繼續輸出。
            hal_pwm_disable(&obj->pwm_hal_adp);
            pwm_running[channel] = 0;
            return;
        }
        if (!pwm_running[channel]) {
            hal_pwm_enable(&obj->pwm_hal_adp);
            pwm_running[channel] = 1;
        }
        return;
    }
    if (pwm_oneshot125[channel]) {
        // 與有刷相同，停止狀態是「啟用中、DUTY 為 0」：ESC 收不到脈衝即停止（推論：DUTY 0 時輸出持續
        // 低電位，未上板確認）。停止不延後：
        // 上一次的設定還沒套用時，等到這個週期結束（最多 1 ms）再寫 0。
        if (pwm_ctrl_pending(channel)) {
            hal_pwm_wait_ctrl_ready(&pwm_objects[channel].pwm_hal_adp);
        }
        (void)pwm_write_oneshot125(channel, 0U);
        return;
    }
    // 停止不延後：通道啟用中而上一次的設定還沒套用時，寫 0 要等到這個週期結束。
    pwmout_t *obj = &pwm_objects[channel];
    pwm_deferred[channel] = 0;
    (void)hal_pwm_set_duty(&obj->pwm_hal_adp, pwm_period_us[channel], 0U, 0U);
    pwm_pulse_us[channel] = 0U;
    pwmout_stop(obj);
    pwm_running[channel] = 0;
}

int rtl8735b_pwm_apply_oneshot125(uint8_t channel, uint16_t duty_ticks)
{
    if (channel >= RTL8735B_RCOUT_CHANNELS || duty_ticks == 0U ||
        duty_ticks >= RTL8735B_ONESHOT125_PERIOD_TICKS ||
        rtl8735b_rcout_pin(channel) == RTL8735B_INVALID_PIN || pwm_sclk_mode[channel]) {
        return -1;
    }
    if (pwm_sclk_clock == PWM_SCLK_40MHZ) {
        return RTL8735B_PWM_ERR_CLOCK;
    }
    if (pwm_emergency_stopped) {
        return -1;
    }
    if (pwm_ensure_initialized(channel) != 0) {
        return -1;
    }
    // 第一次用的通道必須停用中（pwm_ensure_initialized 或之前的一般 PWM 已停止），才能改 tick 來源。
    if (!pwm_oneshot125[channel] && pwm_running[channel]) {
        return -1;
    }
    if (pwm_sclk_clock == PWM_SCLK_UNSET) {
        // pwmout_init 第一次執行時把 PWM SCLK 設成 40 MHz，之後不再設定；所以在它之後才改。
        // 只改 PWM 的 SCLK，G-Timer（一般 PWM 的 tick 來源）的時鐘不變。
        // 4 MHz 來自晶片內部的 RC 振盪器（不是 40 MHz 的 PLL），精度與溫度漂移都未量測；
        // 這個呼叫是否同時開啟該振盪器與 PWM 的 SCLK，也未上板確認。
        hal_pwm_clk_sel(PWM_Sclk_4M);
        pwm_sclk_clock = PWM_SCLK_4MHZ;
    }
    hal_pwm_adapter_t *adp = &pwm_objects[channel].pwm_hal_adp;
    if (!pwm_oneshot125[channel]) {
        hal_pwm_set_clk_sel(adp, PWM_CLK_SEL_SCLK);
        pwm_oneshot125[channel] = 1;
    }

    if (pwm_running[channel] && duty_ticks == pwm_oneshot125_duty[channel]) {
        pwm_deferred[channel] = 0;
        return 0;
    }
    if (pwm_ctrl_pending(channel)) {
        // 同一般 PWM：上一次的設定還沒套用時不等待，記下最新的值由 rtl8735b_pwm_flush 寫入。
        pwm_deferred[channel] = 1;
        pwm_deferred_duty_ticks[channel] = duty_ticks;
        return 0;
    }
    (void)pwm_write_oneshot125(channel, duty_ticks);
    if (pwm_emergency_stopped) {
        // 寫入途中被緊急停止搶先：緊急停止的歸零可能早於上面的寫入，這裡再歸零一次。
        rtl8735b_pwm_stop(channel);
        return -1;
    }
    return 0;
}
