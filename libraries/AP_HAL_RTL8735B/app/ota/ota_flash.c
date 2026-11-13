/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * OTA 讀寫 Flash 的 SDK 端，規則見 ota_core.h。
 *
 * - 讀取用 flash_burst_read（user mode，讀實體位址）。flash_stream_read 是 auto mode，會經過執行中映像的
 *   XIP 位址對映：從 fw2 開機時，讀 fw1 範圍會讀到 fw2 的內容，讀回核對因此失敗。開機試開檢查、分割表、
 *   標籤、試開紀錄與讀回核對都經過這裡，一處讀法就全部涵蓋。
 * - 寫入與抹除用 flash_stream_write、flash_erase_sector，方式同 HAL 的 storage_shim.c：flash_stream_write
 *   （不抹除，只把位元由 1 清成 0）不回報錯誤，寫入前把 32 位元組對齊的暫存寫回 D-cache。
 *   抹除與寫入後都以實體位址讀回核對，寫錯地方會失敗，不會悄悄通過。
 * - task 中以 device_mutex_lock(RT_DEV_LOCK_FLASH) 與 Storage 等其他 Flash 使用者互斥；
 *   開機檢查在排程器啟動前執行，那時沒有其他 task，讀寫都不上鎖。
 * - 容量取自 flash_read_id 第三個 byte（容量的 2 的冪次），同 HAL 的 storage_shim.c。
 */

#include "ota_flash.h"

#include <string.h>

#include <objects.h>
#include <flash_api.h>
#include <device_lock.h>
#include <hal_cache.h>

// NOR Flash 的 page；每次寫入不跨 page 邊界
#define OTA_PROGRAM_CHUNK       256u
#define OTA_VERIFY_CHUNK        64u
// 容量的 2 的冪次只接受 1 MiB 到 2 GiB，超出就當成讀不到
#define OTA_FLASH_MIN_LOG2      20u
#define OTA_FLASH_MAX_LOG2      31u

typedef struct {
    flash_t flash;
    int use_lock;
} ota_flash_ctx_t;

static ota_flash_ctx_t ota_flash_ctx = {.use_lock = 1};
static uint32_t ota_program_words[OTA_PROGRAM_CHUNK / 4] __attribute__((aligned(32)));

static void _lock(const ota_flash_ctx_t *c)
{
    if (c->use_lock) {
        device_mutex_lock(RT_DEV_LOCK_FLASH);
    }
}

static void _unlock(const ota_flash_ctx_t *c)
{
    if (c->use_lock) {
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    }
}

static int _flash_read(void *ctx, uint32_t addr, uint8_t *buf, uint32_t len)
{
    ota_flash_ctx_t *c = (ota_flash_ctx_t *)ctx;
    _lock(c);
    flash_burst_read(&c->flash, addr, len, buf);
    _unlock(c);
    return 0;
}

// expect 為 NULL 時核對每個位元組都等於 fill
static int _flash_verify(ota_flash_ctx_t *c, uint32_t addr, const uint8_t *expect, uint8_t fill, uint32_t len)
{
    uint8_t buf[OTA_VERIFY_CHUNK];
    for (uint32_t off = 0; off < len; off += OTA_VERIFY_CHUNK) {
        const uint32_t n = len - off < OTA_VERIFY_CHUNK ? len - off : OTA_VERIFY_CHUNK;
        _flash_read(c, addr + off, buf, n);
        for (uint32_t i = 0; i < n; i++) {
            if (buf[i] != (expect != NULL ? expect[off + i] : fill)) {
                return -1;
            }
        }
    }
    return 0;
}

static int _flash_erase(void *ctx, uint32_t addr)
{
    ota_flash_ctx_t *c = (ota_flash_ctx_t *)ctx;
    _lock(c);
    flash_erase_sector(&c->flash, addr);
    _unlock(c);
    return _flash_verify(c, addr, NULL, 0xFF, OTA_SECTOR_SIZE);
}

static int _flash_program(void *ctx, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    ota_flash_ctx_t *c = (ota_flash_ctx_t *)ctx;
    for (uint32_t off = 0; off < len;) {
        const uint32_t page_left = OTA_PROGRAM_CHUNK - (addr + off) % OTA_PROGRAM_CHUNK;
        const uint32_t n = len - off < page_left ? len - off : page_left;
        _lock(c);
        memcpy(ota_program_words, buf + off, n);
        // 寫入前先把來源緩衝區的 D-cache 寫回
        dcache_clean_invalidate_by_addr(ota_program_words, (int32_t)OTA_PROGRAM_CHUNK);
        flash_stream_write(&c->flash, addr + off, n, (uint8_t *)ota_program_words);
        _unlock(c);
        off += n;
    }
    // 寫入只能把位元清成 0；目的位置沒有先抹除（或不只清位元）時讀回值會不同，當成失敗
    return _flash_verify(c, addr, buf, 0, len);
}

static const ota_flash_t ota_flash = {_flash_read, _flash_erase, _flash_program, &ota_flash_ctx};

const ota_flash_t *rtl8735b_ota_flash(void)
{
    return &ota_flash;
}

void rtl8735b_ota_flash_use_lock(int use_lock)
{
    ota_flash_ctx.use_lock = use_lock;
}

uint32_t rtl8735b_ota_flash_size(void)
{
    uint8_t id[3] = {0, 0, 0};
    _lock(&ota_flash_ctx);
    const int n = flash_read_id(&ota_flash_ctx.flash, id, sizeof(id));
    _unlock(&ota_flash_ctx);
    if (n < (int)sizeof(id) || id[2] < OTA_FLASH_MIN_LOG2 || id[2] > OTA_FLASH_MAX_LOG2) {
        return 0;
    }
    return 1u << id[2];
}
