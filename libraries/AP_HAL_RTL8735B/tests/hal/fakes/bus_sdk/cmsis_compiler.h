#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/cmsis/cmsis-core/include/cmsis_compiler.h（Arm CMSIS，Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#define __DMB() __asm__ volatile("" ::: "memory")
