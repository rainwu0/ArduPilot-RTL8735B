#pragma once
// 測試替身。出處：名稱與型別依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_sdhost.h 與
// rtl8735b_sdhost.h（Apache-2.0）；只寫受測程式用到的欄位與函式，自己撰寫。結構的版面與正式標頭不同，
// 兩者是否一致由交叉建置把關。
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef enum {
    HAL_OK = 0x00,
    HAL_BUSY = 0x01,
    HAL_TIMEOUT = 0x02,
    HAL_ERR_PARA = 0x03,
    HAL_ERR_HW = 0x05,
} hal_status_t;

typedef void (*sdhost_para_cb_t)(void *para);

typedef struct hal_sdhost_adapter {
    volatile u8 card_inited;
    u8 force_33v;
    u8 verbose;
    u32 magic_num;
    u32 blk_capacity;
    sdhost_para_cb_t task_yield_cb;
    void *task_yield_cb_para;
    void (*dcache_invalidate_by_addr)(uint32_t *addr, int32_t dsize);
    void (*dcache_clean_by_addr)(uint32_t *addr, int32_t dsize);
} hal_sdhost_adapter_t;

typedef struct hal_sdhost_func_stubs_s {
    hal_status_t (*hal_sdhost_init_host)(hal_sdhost_adapter_t *psdhost_adapter);
    hal_status_t (*hal_sdhost_init_card)(hal_sdhost_adapter_t *psdhost_adapter);
    void (*hal_sdhost_deinit)(hal_sdhost_adapter_t *psdhost_adapter);
    hal_status_t (*hal_sdhost_read_data)(hal_sdhost_adapter_t *psdhost_adapter, u32 start_addr, u16 blk_cnt, u8 *rbuf_32align);
    hal_status_t (*hal_sdhost_write_data)(hal_sdhost_adapter_t *psdhost_adapter, u32 start_addr, u16 blk_cnt, const u8 *wbuf_32align);
    void (*hal_sdhost_task_yield_hook)(hal_sdhost_adapter_t *psdhost_adapter, sdhost_para_cb_t task_yield, void *pdata);
} hal_sdhost_func_stubs_t;

extern const hal_sdhost_func_stubs_t hal_sdhost_stubs;

// 正式標頭只有宣告、沒有實作；受測程式不應呼叫它（替身也不提供定義，呼叫了就連結失敗）
hal_status_t hal_sdhost_init_host(hal_sdhost_adapter_t *psdhost_adapter);

static inline hal_status_t hal_sdhost_init_card(hal_sdhost_adapter_t *psdhost_adapter)
{
    return hal_sdhost_stubs.hal_sdhost_init_card(psdhost_adapter);
}

static inline void hal_sdhost_deinit(hal_sdhost_adapter_t *psdhost_adapter)
{
    hal_sdhost_stubs.hal_sdhost_deinit(psdhost_adapter);
}

static inline hal_status_t hal_sdhost_read_data(hal_sdhost_adapter_t *psdhost_adapter, u32 sd_blk_idx, u16 blk_cnt, u8 *rbuf_32align)
{
    return hal_sdhost_stubs.hal_sdhost_read_data(psdhost_adapter, sd_blk_idx, blk_cnt, rbuf_32align);
}

static inline hal_status_t hal_sdhost_write_data(hal_sdhost_adapter_t *psdhost_adapter, u32 sd_blk_idx, u16 blk_cnt, const u8 *wbuf_32align)
{
    return hal_sdhost_stubs.hal_sdhost_write_data(psdhost_adapter, sd_blk_idx, blk_cnt, wbuf_32align);
}

static inline void hal_sdhost_task_yield_hook(hal_sdhost_adapter_t *psdhost_adapter, sdhost_para_cb_t task_yield, void *pdata)
{
    hal_sdhost_stubs.hal_sdhost_task_yield_hook(psdhost_adapter, task_yield, pdata);
}

static inline void hal_sdhost_force33v(hal_sdhost_adapter_t *adpt)
{
    adpt->force_33v = 1;
}

#ifdef __cplusplus
}
#endif
