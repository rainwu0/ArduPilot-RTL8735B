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

static const uint8_t gpio_logical_pins[RTL8735B_GPIO_PIN_COUNT] = {7, 25, 26};
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
// 1 表示此通道已改用 SCLK tick（hal_pwm_set_duty_ns）；本次開機內不再走 mbed 微秒 API。
static uint8_t pwm_sclk_mode[RTL8735B_RCOUT_CHANNELS];
static uint16_t pwm_sclk_period_ticks[RTL8735B_RCOUT_CHANNELS];
// 1 表示已緊急停止（rtl8735b_pwm_emergency_stop）：本次開機內只接受脈寬／duty 0。
// 放在 RCOutput 的 mutex 之下、不依賴 FreeRTOS，之後任何執行緒的非零寫入都擋得住。
static volatile uint8_t pwm_emergency_stopped;

static int gpio_slot(uint8_t logical_pin)
{
    for (uint8_t i = 0; i < RTL8735B_GPIO_PIN_COUNT; i++) {
        if (gpio_logical_pins[i] == logical_pin &&
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
    const uint32_t sdk_pin = rtl8735b_analog_pin(logical_pin);
    if (sdk_pin == RTL8735B_INVALID_PIN || sdk_pin != (uint32_t)PIN_F0) {
        return -1;
    }
    if (!adc_initialized) {
        memset(&adc_adapter, 0, sizeof(adc_adapter));
        hal_adc_load_default(&adc_adapter);
        // 此板 profile 的 PF0 對應 ADC0。
        adc_adapter.plft_dat.pin_en.w |= 1UL;
        hal_status_t status = hal_adc_init(&adc_adapter);
        if (status != HAL_OK) {
            return (int)status;
        }
        hal_delay_ms(20);
        hal_adc_set_in_type_all(&adc_adapter, HP_ADC_INPUT_ALL_SINGLE);
        adc_channel = 0;
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
    if (rtl8735b_analog_pin(logical_pin) == RTL8735B_INVALID_PIN || !adc_initialized) {
        return -1;
    }
    // 與 SDK analogin_read_u16 相同，回傳 12 位元原始碼；讀取 API 不提供失敗狀態。
    return (int32_t)hal_adc_single_read(&adc_adapter, 0);
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
    }
    return 0;
}

int rtl8735b_pwm_apply(uint8_t channel, uint16_t period_us, uint16_t pulse_us)
{
    if (channel >= RTL8735B_RCOUT_CHANNELS || period_us == 0 || pulse_us >= period_us ||
        rtl8735b_rcout_pin(channel) == RTL8735B_INVALID_PIN) {
        return -1;
    }
    // hal_pwm_set_duty_ns 已把此通道的 tick 來源改成 SCLK；mbed 微秒 API 的
    // hal_pwm_set_duty 是否會改回 G-Timer tick 未查證，因此不混用（MOT_PWM_TYPE 本來就需重開機）。
    if (pwm_sclk_mode[channel]) {
        return -1;
    }
    if (pwm_emergency_stopped && pulse_us != 0U) {
        return -1;
    }
    if (pwm_ensure_initialized(channel) != 0) {
        return -1;
    }

    pwmout_t *obj = &pwm_objects[channel];

    pwmout_period_us(obj, (int)period_us);
    pwmout_pulsewidth_us(obj, (int)pulse_us);
    if (!pwm_running[channel]) {
        pwmout_start(obj);
        pwm_running[channel] = 1;
    }
    if (pwm_emergency_stopped && pulse_us != 0U) {
        // 寫入途中被緊急停止搶先：緊急停止的歸零可能早於上面的寫入，這裡再歸零一次。
        rtl8735b_pwm_stop(channel);
        return -1;
    }
    return 0;
}

int rtl8735b_pwm_apply_ticks(uint8_t channel, uint16_t period_ticks, uint16_t duty_ticks)
{
    if (channel >= RTL8735B_RCOUT_CHANNELS || period_ticks == 0U ||
        period_ticks > RTL8735B_PWM_MAX_TICKS || duty_ticks >= period_ticks ||
        rtl8735b_rcout_pin(channel) == RTL8735B_INVALID_PIN) {
        return -1;
    }
    if (pwm_emergency_stopped && duty_ticks != 0U) {
        return -1;
    }
    if (pwm_ensure_initialized(channel) != 0) {
        return -1;
    }

    pwmout_t *obj = &pwm_objects[channel];
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
    pwmout_pulsewidth_us(&pwm_objects[channel], 0);
    pwmout_stop(&pwm_objects[channel]);
    pwm_running[channel] = 0;
}
