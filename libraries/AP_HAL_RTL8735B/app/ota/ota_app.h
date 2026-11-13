/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * Wi-Fi 韌體更新（OTA）的開發板應用程式（hwdef 的 env RTL8735B_APP_DIR libraries/AP_HAL_RTL8735B/app/ota）。
 * 這個目錄實作 app/rtl8735b_app.h 的：
 *
 *   rtl8735b_app_init()            開機試開檢查（排程器啟動前）；試開次數用完就退回另一槽
 *   rtl8735b_app_after_setup()     建立接收端 task
 *   rtl8735b_app_arming_blocked()  傳輸中與試開中擋一般解鎖
 *
 * rtl8735b_app_before_reset() 不在這裡實作（OTA 沒有要放回安全狀態的硬體），用韌體的空版本。OTA 自己要重置時
 * 也會先呼叫它，所以複製這個目錄、加上自己的程式時，在 rtl8735b_app_before_reset() 放回自己的硬體，
 * OTA 的重置一樣涵蓋到（它可能在 FreeRTOS 計時器回呼中被呼叫，照 rtl8735b_app.h 的約定不阻塞、不拿鎖、不印字）。
 *
 * 建置產生、不進版本管理：ota_public_key.c（環境變數 RTL8735B_OTA_PUBLIC_KEY 指向 Tools/scripts/signing/
 * generate_keys.py 產生的公鑰檔），定義下面兩個符號。沒指定時長度是 0，接收端拒收所有更新（NO_KEY）。
 * 私鑰不進韌體，建置過程也不讀私鑰。
 *
 * 接收埠是 RTL8735B_OTA_PORT，沒定義時用 OTA_DEFAULT_PORT。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_DEFAULT_PORT 8266u

extern const uint8_t rtl8735b_ota_public_key[32];
extern const uint32_t rtl8735b_ota_public_key_len;

#ifdef __cplusplus
}
#endif
