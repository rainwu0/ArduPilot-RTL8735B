#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/app/rtl_printf/include/diag.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>

// 替身：真實定義見 SDK component/soc/8735b/app/rtl_printf/include/diag.h:67、87；
// ROM 的 DBG_I2C_ERR 以 ConfigDebugErr & _DBG_I2C_ 決定是否同步列印（diag.h:344-347）。
#define _DBG_I2C_ 0x00000020U

#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t ConfigDebugErr;
#ifdef __cplusplus
}
#endif

#define DBG_ERR_MSG_OFF(x) (ConfigDebugErr &= ~(x))
