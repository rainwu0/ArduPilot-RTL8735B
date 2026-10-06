/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * 硬體看門狗與開機原因的 C 包裝。SDK 標頭在 C++ 下有型別衝突（見 sdk_shim.h），SDK 呼叫放在以 C 編譯的
 * wdt_shim.c。
 * 事實、出處與待確認事項見 wdt_shim.c 檔頭。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 以 SDK mbed wdt_api 設定逾時並啟動看門狗。回傳 0：啟動後 VNDR 看門狗的致能位元讀回為 1；
// -1：讀回不是 1（命令已下，呼叫端仍要照常餵狗）。
int rtl8735b_watchdog_start(uint32_t timeout_ms);
// 餵狗（mbed watchdog_refresh）
void rtl8735b_watchdog_refresh(void);
// 上一次重置是否由 vendor 看門狗造成。第一次呼叫時讀取 AON BOOT_REASON 並清除該位元，之後回傳同一個值；
// HAL_RTL8735B::run() 一開始先呼叫一次，確保在任何程式查詢之前讀到。
uint8_t rtl8735b_reset_was_watchdog(void);

#ifdef __cplusplus
}
#endif
