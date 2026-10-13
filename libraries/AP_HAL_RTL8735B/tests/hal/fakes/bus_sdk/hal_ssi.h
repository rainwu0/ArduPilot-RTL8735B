#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_ssi.h、component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_ssi.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>
typedef struct { uint8_t index; } phal_ssi_adaptor_t;
// SDK rtl8735b_ssi.h 的值
enum { ScpolInactiveIsLow = 0, ScpolInactiveIsHigh = 1 };
enum { ScphToggleInMiddle = 0, ScphToggleAtStart = 1 };
#ifdef __cplusplus
extern "C" {
#endif
hal_status_t hal_ssi_set_sclk(phal_ssi_adaptor_t *, uint32_t hz);
hal_status_t hal_ssi_stop_recv(phal_ssi_adaptor_t *);
hal_status_t hal_ssi_enable(phal_ssi_adaptor_t *);
hal_status_t hal_ssi_disable(phal_ssi_adaptor_t *);
hal_status_t hal_ssi_set_sclk_polarity(phal_ssi_adaptor_t *, uint8_t);
hal_status_t hal_ssi_set_sclk_phase(phal_ssi_adaptor_t *, uint8_t);
#ifdef __cplusplus
}
#endif
