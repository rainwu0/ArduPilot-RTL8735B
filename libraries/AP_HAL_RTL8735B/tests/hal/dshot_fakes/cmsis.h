#pragma once
// 測試替身。出處：名稱依 SDK d1b6426 的 CMSIS（component/soc/8735b/cmsis/cmsis-core/include/core_armv8mml.h、
// cmsis_gcc.h，Apache-2.0）與 platform_conf.h；只寫受測程式用到的部分，自己撰寫。週期計數器的讀取與
// GPIO 暫存器的寫入改由 dshot_shim.c 的兩個巨集呼叫測試的函式，記下每次寫入的時間。
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CONFIG_CPU_CLK (500000000U)
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t CYCCNT;
} DWT_Type;
typedef struct {
    volatile uint32_t DEMCR;
} CoreDebug_Type;
extern DWT_Type fake_dwt;
extern CoreDebug_Type fake_coredebug;
#define DWT (&fake_dwt)
#define CoreDebug (&fake_coredebug)
#define DWT_CTRL_CYCCNTENA_Msk (1UL)
#define DWT_CTRL_NOCYCCNT_Msk (1UL << 25)
#define CoreDebug_DEMCR_TRCENA_Msk (1UL << 24)
uint32_t __get_PRIMASK(void);
void __set_PRIMASK(uint32_t primask);
void __disable_irq(void);
uint32_t fake_cycles_read(void);
void fake_reg_write(volatile uint32_t *reg, uint32_t value);
#define RTL8735B_DSHOT_CYCLES() fake_cycles_read()
#define RTL8735B_DSHOT_REG_WRITE(reg, value) fake_reg_write((reg), (value))
#ifdef __cplusplus
}
#endif
