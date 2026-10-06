/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

#define RTL8735B_RCOUT_CHANNELS 4U
#define RTL8735B_GPIO_PIN_COUNT 3U
#define RTL8735B_ADC_CHANNELS 1U
#define RTL8735B_ADC_LOGICAL0 2U
#define RTL8735B_INVALID_PIN UINT32_MAX

// 有刷馬達 PWM 以 PWM SCLK（40 MHz，每 tick 25 ns）為時基；PERIOD／DUTY 暫存器為 12 位元。
// 依據：SDK 的 rtl8735b_pwm_type.h（CLK_SEL 8 為 sclk、DUTY／PERIOD 12 位元），
// hal_pwm_set_duty_ns 以 25 ns 為單位並拒絕超過 102375 ns 的週期。
#define RTL8735B_PWM_SCLK_HZ 40000000UL
#define RTL8735B_PWM_SCLK_TICK_NS 25U
#define RTL8735B_PWM_MAX_TICKS 4095U
// 下限取整數且高於硬體下限（40 MHz / 4095 ≈ 9768 Hz）；上限讓每週期至少 1250 階。
#define RTL8735B_RC_BRUSHED_MIN_HZ 10000U
#define RTL8735B_RC_BRUSHED_MAX_HZ 32000U

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

#ifdef __cplusplus
}
#endif
