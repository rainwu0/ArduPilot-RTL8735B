#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_cache.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void dcache_invalidate_by_addr(uint32_t *addr, int32_t dsize);
void dcache_clean_by_addr(uint32_t *addr, int32_t dsize);
#ifdef __cplusplus
}
#endif
