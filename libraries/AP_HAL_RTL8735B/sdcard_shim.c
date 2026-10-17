/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 以 C 編譯的 microSD 區塊存取，介面見 sdcard_shim.h。
 *
 * SD 主控驅動：hal_sdhost.h 的讀寫與識別是經 hal_sdhost_stubs 函式表的 inline 呼叫。SDK d1b6426 沒有附
 * hal_sdhost.c，hal_sdhost_init_host 只有宣告；公開 SDK（Ameba-AIoT/ameba-rtos-pro2 eb5c0907c40d）的
 * component/soc/8735b/fwlib/rtl8735b/source/ram/hal_sdhost.c 把它實作成轉呼函式表的同名成員，
 * 這裡照做，直接呼叫函式表。
 *
 * 腳位由驅動設定。訊號固定 3.3 V（force_33v），不切 1.8 V：推論，公開的板子資料沒有寫 SD 訊號能用 1.8 V；
 * 能推翻的證據是板上實測 1.8 V 模式讀寫正常。
 */

#include "sdcard_shim.h"

#include <stdint.h>
#include <string.h>

#include <hal_sdhost.h>
#include <hal_cache.h>
#include <FreeRTOS.h>
#include <task.h>

// 驅動一次最多傳的區塊數：read_data／write_data 的 blk_cnt 是 u16
#define SDCARD_MAX_XFER_BLOCKS 0xFFFFu
// 驅動要求緩衝區 32 位元組對齊（hal_sdhost.h 的 rbuf_32align、wbuf_32align），D-cache 也以 32 位元組的
// 快取列為單位維護；呼叫者的緩衝區沒有對齊時經這塊暫存轉一手
#define SDCARD_BOUNCE_BLOCKS 8u

static hal_sdhost_adapter_t sdcard_adapter;
// 以 uint32_t 宣告，dcache 函式的 uint32_t * 參數不需要提高對齊的轉型
static uint32_t sdcard_bounce[SDCARD_BOUNCE_BLOCKS * RTL8735B_SDCARD_BLOCK_SIZE / 4] __attribute__((aligned(32)));
static uint8_t sdcard_inited;
static uint32_t sdcard_blocks;

// 驅動等卡忙碌時呼叫（hal_sdhost.h 的 hal_sdhost_task_yield_hook）。讓出至少一個 tick：呼叫者常是
// AP_Logger 的記錄執行緒（優先權與主執行緒同為 4），忙等會讓優先權較低的 task 拿不到 CPU
// （Scheduler.cpp 的 delay_microseconds 註解有板上實測）
static void _yield(void *para)
{
    (void)para;
    vTaskDelay(1);
}

// 驅動是否用 adapter 的回呼做快取維護，SDK d1b6426 沒有原始碼可查，所以本層自己也做；重複做無害
static void _cache_clean(const void *buf, uint32_t len)
{
    dcache_clean_by_addr((uint32_t *)(uintptr_t)buf, (int32_t)len);
}

static void _cache_invalidate(void *buf, uint32_t len)
{
    dcache_invalidate_by_addr((uint32_t *)(uintptr_t)buf, (int32_t)len);
}

int rtl8735b_sdcard_init(void)
{
    rtl8735b_sdcard_deinit();
    memset(&sdcard_adapter, 0, sizeof(sdcard_adapter));
    if (hal_sdhost_stubs.hal_sdhost_init_host(&sdcard_adapter) != HAL_OK) {
        return RTL8735B_SDCARD_EHOST;
    }
    // 選項在 init_host 之後、init_card 之前設定（hal_sdhost.h 的 hal_sdhost_force33v 註明在 init_card 之前）
    hal_sdhost_force33v(&sdcard_adapter);
    sdcard_adapter.dcache_invalidate_by_addr = dcache_invalidate_by_addr;
    sdcard_adapter.dcache_clean_by_addr = dcache_clean_by_addr;
    hal_sdhost_task_yield_hook(&sdcard_adapter, _yield, NULL);
    if (hal_sdhost_init_card(&sdcard_adapter) != HAL_OK || sdcard_adapter.blk_capacity == 0) {
        hal_sdhost_deinit(&sdcard_adapter);
        return RTL8735B_SDCARD_ECARD;
    }
    sdcard_blocks = sdcard_adapter.blk_capacity;
    sdcard_inited = 1;
    return RTL8735B_SDCARD_OK;
}

void rtl8735b_sdcard_deinit(void)
{
    if (sdcard_inited) {
        hal_sdhost_deinit(&sdcard_adapter);
    }
    sdcard_inited = 0;
    sdcard_blocks = 0;
}

int rtl8735b_sdcard_ready(void)
{
    return sdcard_inited;
}

uint32_t rtl8735b_sdcard_block_count(void)
{
    return rtl8735b_sdcard_ready() ? sdcard_blocks : 0;
}

static int _check(uint32_t block, const void *buf, uint32_t count)
{
    if (buf == NULL) {
        return RTL8735B_SDCARD_EINVAL;
    }
    if (!rtl8735b_sdcard_ready()) {
        return RTL8735B_SDCARD_ENOCARD;
    }
    if (block > sdcard_blocks || count > sdcard_blocks - block) {
        return RTL8735B_SDCARD_EINVAL;
    }
    return RTL8735B_SDCARD_OK;
}

static int _aligned(const void *buf)
{
    return ((uintptr_t)buf & 31u) == 0;
}

int rtl8735b_sdcard_read(uint32_t block, uint8_t *buf, uint32_t count)
{
    const int ret = _check(block, buf, count);
    if (ret != RTL8735B_SDCARD_OK) {
        return ret;
    }
    const int direct = _aligned(buf);
    while (count > 0) {
        const uint32_t max = direct ? SDCARD_MAX_XFER_BLOCKS : SDCARD_BOUNCE_BLOCKS;
        const uint32_t n = count < max ? count : max;
        const uint32_t len = n * RTL8735B_SDCARD_BLOCK_SIZE;
        uint8_t *dst = direct ? buf : (uint8_t *)sdcard_bounce;
        // 傳輸前先作廢：避免傳輸期間快取把舊內容寫回、蓋掉 DMA 寫進的資料
        _cache_invalidate(dst, len);
        if (hal_sdhost_read_data(&sdcard_adapter, block, (u16)n, dst) != HAL_OK) {
            return RTL8735B_SDCARD_EIO;
        }
        _cache_invalidate(dst, len);
        if (!direct) {
            memcpy(buf, sdcard_bounce, len);
        }
        block += n;
        buf += len;
        count -= n;
    }
    return RTL8735B_SDCARD_OK;
}

int rtl8735b_sdcard_write(uint32_t block, const uint8_t *buf, uint32_t count)
{
    const int ret = _check(block, buf, count);
    if (ret != RTL8735B_SDCARD_OK) {
        return ret;
    }
    const int direct = _aligned(buf);
    while (count > 0) {
        const uint32_t max = direct ? SDCARD_MAX_XFER_BLOCKS : SDCARD_BOUNCE_BLOCKS;
        const uint32_t n = count < max ? count : max;
        const uint32_t len = n * RTL8735B_SDCARD_BLOCK_SIZE;
        const uint8_t *src = buf;
        if (!direct) {
            memcpy(sdcard_bounce, buf, len);
            src = (const uint8_t *)sdcard_bounce;
        }
        _cache_clean(src, len);
        if (hal_sdhost_write_data(&sdcard_adapter, block, (u16)n, src) != HAL_OK) {
            return RTL8735B_SDCARD_EIO;
        }
        block += n;
        buf += len;
        count -= n;
    }
    return RTL8735B_SDCARD_OK;
}
