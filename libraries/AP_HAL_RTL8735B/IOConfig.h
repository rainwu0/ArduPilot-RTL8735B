/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

#include "hwdef.h"

// 數量來自板子的 hwdef（RTL8735B_RCOUT、RTL8735B_GPIO、RTL8735B_ADC 關鍵字，由 rtl8735b_hwdef.py 產生）
#define RTL8735B_RCOUT_CHANNELS HAL_RTL8735B_RCOUT_COUNT
#define RTL8735B_GPIO_PIN_COUNT HAL_RTL8735B_GPIO_COUNT
#define RTL8735B_ADC_CHANNELS HAL_RTL8735B_ADC_COUNT
#define RTL8735B_INVALID_PIN UINT32_MAX
#define RTL8735B_INVALID_LOGICAL_PIN 0xFFU

// 一般 PWM（含 OneShot 的連續輸出）的頻率範圍。
#define RTL8735B_RC_MIN_HZ 50U
#define RTL8735B_RC_MAX_HZ 490U

// 有刷馬達 PWM 以 PWM SCLK（40 MHz，每 tick 25 ns）為時基；PERIOD／DUTY 暫存器為 12 位元。
// 依據：SDK 的 rtl8735b_pwm_type.h（CLK_SEL 8 為 sclk、DUTY／PERIOD 12 位元），
// hal_pwm_set_duty_ns 以 25 ns 為單位並拒絕超過 102375 ns 的週期。
#define RTL8735B_PWM_SCLK_HZ 40000000UL
#define RTL8735B_PWM_SCLK_TICK_NS 25U
#define RTL8735B_PWM_MAX_TICKS 4095U
// 下限取整數且高於硬體下限（40 MHz / 4095 ≈ 9768 Hz）；上限讓每週期至少 1250 階。
#define RTL8735B_RC_BRUSHED_MIN_HZ 10000U
#define RTL8735B_RC_BRUSHED_MAX_HZ 32000U
// OneShot125：PWM SCLK 改用 4 MHz（每 tick 250 ns），以固定 1 kHz 連續輸出。
#define RTL8735B_ONESHOT125_SCLK_HZ 4000000UL
#define RTL8735B_ONESHOT125_HZ 1000U
#define RTL8735B_ONESHOT125_PERIOD_TICKS 4000U

typedef struct {
    uint16_t pulse_us;
    uint16_t period_us;
    uint8_t enabled;
    uint8_t has_valid_write;
    uint8_t mode_supported;
    uint8_t safety_disarmed;
} RTL8735BRCChannelPolicy;

#ifdef __cplusplus
extern "C" {
#endif

uint32_t rtl8735b_rcout_pin(uint8_t channel);
uint32_t rtl8735b_gpio_pin(uint8_t logical_pin);
uint8_t rtl8735b_gpio_output_allowed(uint8_t logical_pin);
uint32_t rtl8735b_analog_pin(uint8_t logical_pin);
// 第 slot 個 GPIO 的邏輯編號；超出範圍回傳 RTL8735B_INVALID_LOGICAL_PIN。
uint8_t rtl8735b_gpio_logical_pin(uint8_t slot);
// 第 index 個類比輸入的邏輯編號，以及邏輯編號對應的 ADC 通道；找不到回傳 RTL8735B_INVALID_LOGICAL_PIN。
uint8_t rtl8735b_adc_logical_pin(uint8_t index);
uint8_t rtl8735b_adc_channel(uint8_t logical_pin);
uint8_t rtl8735b_rcout_channel_count(void);
uint8_t rtl8735b_adc_channel_count(void);
uint8_t rtl8735b_gpio_pin_count(void);

uint8_t rtl8735b_rc_rate_valid(uint16_t rate_hz);
uint8_t rtl8735b_rc_pulse_valid(uint16_t pulse_us, uint16_t period_us);
uint8_t rtl8735b_rc_can_emit(const RTL8735BRCChannelPolicy *channel);

uint8_t rtl8735b_rc_brushed_rate_valid(uint16_t rate_hz);
// 回傳 SCLK tick 數；頻率不在有刷範圍時回傳 0。
uint16_t rtl8735b_rc_brushed_period_ticks(uint16_t rate_hz);
// 依 ESC 範圍把脈寬換成 duty tick：pulse <= min 為 0，pulse >= max 為 period_ticks - 1，其間線性（無條件捨去）。
uint16_t rtl8735b_rc_brushed_duty_ticks(uint16_t pulse_us, uint16_t esc_min_us,
                                         uint16_t esc_max_us, uint16_t period_ticks);
// OneShot125 的 duty tick：pulse_us / 8 微秒、4 MHz，即 pulse_us / 2；脈寬不在 400–2100 us 時回傳 0。
uint16_t rtl8735b_rc_oneshot125_duty_ticks(uint16_t pulse_us);

#ifdef __cplusplus
}
#endif
