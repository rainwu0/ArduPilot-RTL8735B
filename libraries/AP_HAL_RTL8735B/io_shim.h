/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int rtl8735b_gpio_mode(uint8_t logical_pin, uint8_t output);
int rtl8735b_gpio_read(uint8_t logical_pin);
int rtl8735b_gpio_write(uint8_t logical_pin, uint8_t value);
typedef void (*rtl8735b_gpio_irq_handler)(uint32_t irq_id, uint8_t value, uint32_t timestamp_us);
int rtl8735b_gpio_irq_attach(uint8_t logical_pin, uint8_t trigger, uint32_t irq_id,
                              rtl8735b_gpio_irq_handler handler);
void rtl8735b_gpio_irq_detach(uint8_t logical_pin);

int rtl8735b_adc_init(uint8_t logical_pin);
int32_t rtl8735b_adc_read_u16(uint8_t logical_pin);

int rtl8735b_pwm_apply(uint8_t channel, uint16_t period_us, uint16_t pulse_us);
// 以 PWM SCLK tick（25 ns）設定週期與 duty；duty_ticks < period_ticks <= RTL8735B_PWM_MAX_TICKS。
// duty_ticks 為 0 時通道仍保持啟用、DUTY 暫存器為 0（有刷馬達的停止狀態）。
// 通道一旦改用 SCLK tick，本次開機內 rtl8735b_pwm_apply 對該通道回傳失敗。
int rtl8735b_pwm_apply_ticks(uint8_t channel, uint16_t period_ticks, uint16_t duty_ticks);
// 一般 PWM 通道：脈寬設 0 後停用。SCLK tick 通道：DUTY 設 0 並保持啟用；寫入失敗時才停用。
void rtl8735b_pwm_stop(uint8_t channel);
// 緊急停止（AP_HAL::panic 第一步呼叫）：已初始化的通道全部依 rtl8735b_pwm_stop 歸零，
// 之後本次開機內 rtl8735b_pwm_apply／rtl8735b_pwm_apply_ticks 的非零輸出一律回傳失敗。
// 不取 RTOS 鎖、不呼叫 FreeRTOS；SDK 端只有暫存器寫入與等待 CTRL_SET 的 ROM 函式
// （推論：ROM 的 hal_pwm_wait_ctrl_ready 是輪詢硬體位元，關中斷時也會返回；SDK 未附原始碼）。
void rtl8735b_pwm_emergency_stop(void);

#ifdef __cplusplus
}
#endif
