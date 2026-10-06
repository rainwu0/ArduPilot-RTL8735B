/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 以 C 編譯的外部 NOR Flash 包裝，理由與區間見 storage_shim.h。
 *
 * component/mbed/targets/hal/rtl8735b/flash_api.c：
 *   - flash_erase_sector／flash_stream_write 沒有回傳錯誤（前者 void，後者固定回傳 1），
 *     所以每次寫入與抹除後都讀回比對，失敗不當成功；
 *   - flash_resource_lock／unlock 在 component/soc/8735b/misc/driver/flash_api_ext.c 是空函式，
 *     SDK 本身不做互斥。映像中其他 Flash 使用者（Wi-Fi 快速連線資料、ftl 的 NOR 回呼）以
 *     device_mutex_lock(RT_DEV_LOCK_FLASH) 包住每次操作，
 *     這裡用同一把鎖，因此只能在 task 中呼叫。
 * 程式由 bootloader 載入 SRAM／DDR 執行（rtl8735b_ram.ld 的 MEMORY 沒有 Flash 區域），
 * 抹除與寫入 NOR 時不會讓正在執行的程式失去指令來源。
 */

#include "storage_shim.h"

#include <stdint.h>
#include <string.h>

#include <objects.h>
#include <flash_api.h>
#include <device_lock.h>
#include <hal_cache.h>

// JEDEC ID 第三個位元組是容量的 log2（Winbond、Macronix、GigaDevice 的慣例）。推論：板上 NOR 依此編碼；
// 不符時 init 失敗、Storage 退回不持久化，不會寫到錯的位置。
#define STORAGE_FLASH_MIN_CAPACITY_LOG2 24u
#define STORAGE_FLASH_MAX_CAPACITY_LOG2 32u

// 寫入前的對齊暫存：SDK hal_cache.h 規定 dcache_*_by_addr 的位址以 32 位元組對齊
#define STORAGE_FLASH_CHUNK 256u
#define STORAGE_FLASH_VERIFY_CHUNK 64u

static flash_t storage_flash;
// 以 uint32_t 宣告，dcache 函式的 uint32_t * 參數不需要提高對齊的轉型
static uint32_t storage_program_words[STORAGE_FLASH_CHUNK / 4] __attribute__((aligned(32)));

static int _range_ok(uint32_t offset, uint32_t len)
{
    return offset <= RTL8735B_STORAGE_FLASH_SIZE && len <= RTL8735B_STORAGE_FLASH_SIZE - offset;
}

int rtl8735b_storage_flash_init(uint8_t id[3])
{
    uint8_t buf[3] = {0, 0, 0};
    if (id == NULL) {
        return RTL8735B_STORAGE_FLASH_EINVAL;
    }
    if (flash_read_id(&storage_flash, buf, sizeof(buf)) < 3) {
        return RTL8735B_STORAGE_FLASH_EID;
    }
    memcpy(id, buf, sizeof(buf));
    if (buf[2] < STORAGE_FLASH_MIN_CAPACITY_LOG2 || buf[2] > STORAGE_FLASH_MAX_CAPACITY_LOG2) {
        return RTL8735B_STORAGE_FLASH_EID;
    }
    // 容量至少 16 MiB 時，區間結尾 0xFA0000 一定在晶片內
    return RTL8735B_STORAGE_FLASH_OK;
}

static void _read_locked(uint32_t address, uint8_t *data, uint32_t len)
{
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_stream_read(&storage_flash, address, len, data);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
}

int rtl8735b_storage_flash_read(uint32_t offset, uint8_t *data, uint32_t len)
{
    if ((data == NULL && len != 0) || !_range_ok(offset, len)) {
        return RTL8735B_STORAGE_FLASH_EINVAL;
    }
    if (len != 0) {
        _read_locked(RTL8735B_STORAGE_FLASH_BASE + offset, data, len);
    }
    return RTL8735B_STORAGE_FLASH_OK;
}

static int _verify(uint32_t address, const uint8_t *expected, uint8_t fill, uint32_t len)
{
    uint8_t back[STORAGE_FLASH_VERIFY_CHUNK];
    while (len > 0) {
        const uint32_t n = len < sizeof(back) ? len : (uint32_t)sizeof(back);
        _read_locked(address, back, n);
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t want = expected != NULL ? expected[i] : fill;
            if (back[i] != want) {
                return RTL8735B_STORAGE_FLASH_EVERIFY;
            }
        }
        address += n;
        len -= n;
        if (expected != NULL) {
            expected += n;
        }
    }
    return RTL8735B_STORAGE_FLASH_OK;
}

int rtl8735b_storage_flash_program(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if ((data == NULL && len != 0) || !_range_ok(offset, len)) {
        return RTL8735B_STORAGE_FLASH_EINVAL;
    }
    uint32_t address = RTL8735B_STORAGE_FLASH_BASE + offset;
    const uint8_t *src = data;
    uint32_t left = len;
    while (left > 0) {
        const uint32_t n = left < STORAGE_FLASH_CHUNK ? left : STORAGE_FLASH_CHUNK;
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        memcpy(storage_program_words, src, n);
        // 寫入前先把來源緩衝區的 D-cache 寫回
        dcache_clean_invalidate_by_addr(storage_program_words, (int32_t)STORAGE_FLASH_CHUNK);
        flash_stream_write(&storage_flash, address, n, (uint8_t *)storage_program_words);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        address += n;
        src += n;
        left -= n;
    }
    return _verify(RTL8735B_STORAGE_FLASH_BASE + offset, data, 0, len);
}

int rtl8735b_storage_flash_erase(uint32_t offset, uint32_t len)
{
    if (!_range_ok(offset, len) || len == 0 ||
        (offset % RTL8735B_STORAGE_FLASH_ERASE_SIZE) != 0 ||
        (len % RTL8735B_STORAGE_FLASH_ERASE_SIZE) != 0) {
        return RTL8735B_STORAGE_FLASH_EINVAL;
    }
    for (uint32_t done = 0; done < len; done += RTL8735B_STORAGE_FLASH_ERASE_SIZE) {
        const uint32_t address = RTL8735B_STORAGE_FLASH_BASE + offset + done;
        // 每個 4 KiB 扇區各自取放鎖，讓其他 Flash 使用者不必等整段抹除完
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&storage_flash, address);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        const int ret = _verify(address, NULL, 0xFF, RTL8735B_STORAGE_FLASH_ERASE_SIZE);
        if (ret != RTL8735B_STORAGE_FLASH_OK) {
            return ret;
        }
    }
    return RTL8735B_STORAGE_FLASH_OK;
}
