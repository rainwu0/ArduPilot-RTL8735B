/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * OTA 讀寫 Flash 的 SDK 端，以便用假的 flash_api 在主機上測試；由 ota_app.c 使用。
 */

#include <stdint.h>

#include "ota_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// ota_core.c 用的讀、抹除、寫入
const ota_flash_t *rtl8735b_ota_flash(void);
// 0：開機檢查（排程器啟動前，只有一個執行緒）不上鎖；其他時候上鎖
void rtl8735b_ota_flash_use_lock(int use_lock);
// Flash 的實際容量（bytes），由 JEDEC ID 的容量欄位換算；讀不到或不合理時回傳 0
uint32_t rtl8735b_ota_flash_size(void);

#ifdef __cplusplus
}
#endif
