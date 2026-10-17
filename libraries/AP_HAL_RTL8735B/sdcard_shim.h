/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * 板上 microSD 的區塊存取，經 SDK 的 SD 主控驅動（hal_sdhost.h）。SDK 標頭經 cmsis.h 引入，
 * 理由同 storage_shim.h，SDK 呼叫放在以 C 編譯的 sdcard_shim.c。
 *
 * - 一個區塊 512 bytes；位址以區塊計。
 * - 不可重入：呼叫者要自己互斥（檔案系統後端以它的號誌包住所有操作）。只在 task 中呼叫：
 *   等待傳輸時會以 vTaskDelay 讓出 CPU。
 * - 讀寫失敗不改變初始化的狀態；要不要關掉重來由呼叫者決定（檔案系統後端在讀寫出錯時關掉再重掛）。
 */

#include <stdint.h>

#define RTL8735B_SDCARD_BLOCK_SIZE 512u

enum {
    RTL8735B_SDCARD_OK = 0,
    // 參數錯誤或超出卡的容量：沒有碰卡
    RTL8735B_SDCARD_EINVAL = -1,
    // 沒有初始化
    RTL8735B_SDCARD_ENOCARD = -2,
    // SD 主控初始化失敗
    RTL8735B_SDCARD_EHOST = -3,
    // 卡的識別失敗（沒有插卡，或卡回應無效、容量為 0）
    RTL8735B_SDCARD_ECARD = -4,
    // 讀寫失敗
    RTL8735B_SDCARD_EIO = -5,
};

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 SD 主控並識別卡；已初始化時先關掉再重來
int rtl8735b_sdcard_init(void);
// 關掉 SD 主控；之後讀寫回傳 ENOCARD
void rtl8735b_sdcard_deinit(void);
// 1：已初始化
int rtl8735b_sdcard_ready(void);
// 卡的容量（區塊數）；沒有初始化時為 0
uint32_t rtl8735b_sdcard_block_count(void);
// 讀寫 count 個區塊；buf 不必對齊，count 為 0 時不做事
int rtl8735b_sdcard_read(uint32_t block, uint8_t *buf, uint32_t count);
int rtl8735b_sdcard_write(uint32_t block, const uint8_t *buf, uint32_t count);

#ifdef __cplusplus
}
#endif
