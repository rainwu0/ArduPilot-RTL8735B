#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_pwm.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
// PWM 暫存器的位元位置取自 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_pwm_type.h；該檔沒有授權標示，這裡只寫 io_shim.c 用到的幾個位元位置。
#include "objects.h"
// 欄位與 SDK rtl8735b_pwm_type.h 相同。
#define PWM_BIT_CTRL_SET ((uint32_t)0x00000001 << 31)
#define PWM_MASK_DUTY ((uint32_t)0x00000FFF << 0)
#define PWM_MASK_DUTY_START ((uint32_t)0x00000FFF << 16)
#define PWM_MASK_PERIOD ((uint32_t)0x00000FFF << 0)
#define PWM_Sclk_4M 1
#define PWM_Sclk_40M 2
typedef uint8_t pwm_clk_sel_t;
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned io_fake_pwm_update_ctrl_calls;
void hal_pwm_enable(hal_pwm_adapter_t *ppwm_adp);
void hal_pwm_disable(hal_pwm_adapter_t *ppwm_adp);
void hal_pwm_clk_sel(int sclk);
void hal_pwm_set_clk_sel(hal_pwm_adapter_t *ppwm_adp, pwm_clk_sel_t clk_sel);
void hal_pwm_wait_ctrl_ready(hal_pwm_adapter_t *ppwm_adp);
hal_status_t hal_pwm_set_duty(hal_pwm_adapter_t *ppwm_adp, uint32_t period_us,
                              uint32_t duty_us, uint32_t start_offset_us);
hal_status_t hal_pwm_set_duty_ns(hal_pwm_adapter_t *ppwm_adp, uint32_t period_ns,
                                 uint32_t duty_ns, uint32_t start_offset_ns);
#ifdef __cplusplus
}
#endif

// SDK hal_pwm.h 的 inline 暫存器寫入，寫法相同。
static inline void hal_pwm_update_ctrl(hal_pwm_adapter_t *ppwm_adp)
{
    io_fake_pwm_update_ctrl_calls++;
    ppwm_adp->base_addr->PWM_CTRL |= PWM_BIT_CTRL_SET;
}

static inline void hal_pwm_set_duty_size(hal_pwm_adapter_t *ppwm_adp, uint32_t duty_ticks)
{
    PWM_TypeDef *regs = ppwm_adp->base_addr;
    regs->PWM_CTRL = (regs->PWM_CTRL & ~PWM_MASK_DUTY) | (duty_ticks & PWM_MASK_DUTY);
}

static inline void hal_pwm_set_onduty_start(hal_pwm_adapter_t *ppwm_adp, uint32_t start_ticks)
{
    PWM_TypeDef *regs = ppwm_adp->base_addr;
    regs->PWM_TIMING_CTRL = (regs->PWM_TIMING_CTRL & ~PWM_MASK_DUTY_START) |
        ((start_ticks << 16) & PWM_MASK_DUTY_START);
}

static inline void hal_pwm_set_period(hal_pwm_adapter_t *ppwm_adp, uint32_t period_ticks)
{
    PWM_TypeDef *regs = ppwm_adp->base_addr;
    regs->PWM_TIMING_CTRL = (regs->PWM_TIMING_CTRL & ~PWM_MASK_PERIOD) | (period_ticks & PWM_MASK_PERIOD);
}
