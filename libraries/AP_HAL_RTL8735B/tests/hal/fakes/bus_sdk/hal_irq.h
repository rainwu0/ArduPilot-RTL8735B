#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_irq.h、
// component/soc/8735b/cmsis/rtl8735b/include/rtl8735b.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>

#define I2C0_IRQn 14

#ifdef __cplusplus
extern "C" {
#endif
void hal_irq_enable(int32_t irqn);
void hal_irq_disable(int32_t irqn);
void hal_irq_clear_pending(int32_t irqn);
#ifdef __cplusplus
}
#endif
