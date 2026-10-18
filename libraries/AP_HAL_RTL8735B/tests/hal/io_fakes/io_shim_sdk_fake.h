#pragma once
#include <stdint.h>
#include "objects.h"
#ifdef __cplusplus
extern "C" {
#endif
extern int io_fake_adc_init_status;
extern int io_fake_adc_cvlist_status;
// 各 ADC 通道（0–7）讀到的值；只有初始化時 pin_en 打開的通道讀得到，其他讀到 0
extern uint16_t io_fake_adc_values[8];
extern unsigned io_fake_adc_init_calls;
// ADC：初始化時的 pin_en、轉換清單的通道、最後一次讀取的通道
extern uint32_t io_fake_adc_pin_en;
extern uint8_t io_fake_adc_cvlist_channel;
extern uint8_t io_fake_adc_read_channel;
extern unsigned io_fake_gpio_init_calls;
extern unsigned io_fake_pwm_init_calls;
extern unsigned io_fake_pwm_start_calls;
extern unsigned io_fake_pwm_stop_calls;
extern unsigned io_fake_pwm_period_calls;
extern unsigned io_fake_pwm_pulse_calls;
extern unsigned io_fake_gpio_write_calls;
extern unsigned io_fake_gpio_dir_calls;
extern unsigned io_fake_gpio_irq_deinit_calls;
extern unsigned io_fake_gpio_irq_init_calls;
extern unsigned io_fake_last_gpio_order_write;
extern unsigned io_fake_last_gpio_order_dir;
extern unsigned io_fake_order_pwm_init;
extern unsigned io_fake_order_pwm_stop;
extern unsigned io_fake_order_pwm_period;
extern unsigned io_fake_order_pwm_pulse;
extern unsigned io_fake_order_pwm_start;
extern int io_fake_pwm_running[12];
extern uint16_t io_fake_pwm_period[12];
extern uint16_t io_fake_pwm_pulse[12];
extern unsigned io_fake_pwm_enable_calls;
extern unsigned io_fake_pwm_disable_calls;
extern unsigned io_fake_pwm_duty_ns_calls;
extern unsigned io_fake_order_pwm_duty_ns;
extern unsigned io_fake_order_pwm_enable;
extern unsigned io_fake_order_pwm_disable;
extern int io_fake_pwm_duty_ns_status;
extern uint32_t io_fake_pwm_period_ns[12];
extern uint32_t io_fake_pwm_duty_ns[12];
extern uint32_t io_fake_pwm_offset_ns[12];
// hal_pwm_set_duty（微秒，ROM）：呼叫次數、順序、回傳值；通道啟用中而 CTRL_SET 未清除時呼叫，
// ROM 會等到週期結束（推論），計入 io_fake_pwm_duty_us_waits。
extern unsigned io_fake_pwm_duty_us_calls;
extern unsigned io_fake_pwm_duty_us_waits;
extern unsigned io_fake_order_pwm_duty_us;
extern int io_fake_pwm_duty_us_status;
// OneShot125：全域 PWM SCLK 的選擇、各通道的 CLK_SEL、hal_pwm_wait_ctrl_ready 的呼叫次數。
extern unsigned io_fake_pwm_sclk_sel_calls;
extern int io_fake_pwm_sclk;
extern unsigned io_fake_order_pwm_sclk_sel;
extern uint8_t io_fake_pwm_clk_sel[4];
extern unsigned io_fake_pwm_wait_ready_calls;
extern unsigned io_fake_pwm_update_ctrl_calls;
extern uint32_t io_fake_timestamp_us;
extern int io_fake_gpio_irq_init_status;
// 非 NULL 時，hal_pwm_set_duty_ns 收到非零 duty、在記錄之前先呼叫一次（隨即清除），
// 模擬寫入途中被其他執行緒（例如 AP_HAL::panic）搶先。
extern void (*io_fake_pwm_duty_ns_preempt)(void);
void io_fake_reset(void);
// 模擬硬體在週期結束時套用新設定、清除 CTRL_SET（只在通道啟用中）。
void io_fake_pwm_period_end(int index);
// 通道的模擬暫存器（DUTY、PERIOD 等欄位）。
PWM_TypeDef *io_fake_pwm_regs(int index);
void io_fake_fire_irq(uint32_t id, int event);
#ifdef __cplusplus
}
#endif
