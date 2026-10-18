/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * DShot 的送幀：CPU 直接寫 GPIO 埠的「寫 1 的位元輸出高」「寫 1 的位元輸出低」兩個暫存器，同一個埠上的
 * 通道同時送。SDK 標頭經 cmsis.h 引入，理由同 storage_shim.h，SDK 呼叫放在以 C 編譯的 dshot_shim.c。
 *
 * - 不可重入：呼叫者要自己互斥（RCOutput 的號誌）。只在 task 中呼叫。
 * - 一幀期間關中斷：DShot150 約 107 µs、300 約 53 µs、600 約 27 µs。
 * - 腳一旦接上，本次開機內維持 GPIO，不還給 PWM。
 */

#include <stdint.h>

#include "dshot_frame.h"

// 槽位上限：同 hwdef 的 PWM 輸出上限
#define RTL8735B_DSHOT_MAX_SLOTS 12u
// 兩幀之間至少間隔（ArduPilot ChibiOS 的 RCOutput 也用 40 µs）
#define RTL8735B_DSHOT_MIN_GAP_US 40u

enum {
    RTL8735B_DSHOT_OK = 0,
    // 參數錯誤，或槽位已滿
    RTL8735B_DSHOT_EINVAL = -1,
    // 腳和先接上的腳不在同一個 GPIO 埠
    RTL8735B_DSHOT_EPORT = -2,
    // 腳不能設成 GPIO（例如已被其他功能佔用）
    RTL8735B_DSHOT_EGPIO = -3,
    // 沒有 CPU 週期計數器，或量到的 CPU 頻率不對
    RTL8735B_DSHOT_ECLOCK = -4,
    // 已緊急停止
    RTL8735B_DSHOT_ESTOPPED = -5,
};

#ifdef __cplusplus
extern "C" {
#endif

// 把腳（SDK 的晶片腳編號）設成 GPIO 輸出低電位，回傳槽位（0 起）或負的錯誤碼；同一支腳再呼叫回傳同一個
// 槽位。第一次呼叫時校正 CPU 週期計數器，失敗之後每次都回傳 ECLOCK。
int rtl8735b_dshot_attach(uint32_t pin);
// 校正通過時的 CPU 頻率（Hz），否則為 0
uint32_t rtl8735b_dshot_cpu_hz(void);
// 在 active 指定的槽位（位元 i 是槽位 i）同時送一幀，frames[i] 是槽位 i 的幀；其他接上的腳維持低電位。
// 距上一幀結束不足 RTL8735B_DSHOT_MIN_GAP_US 時先等。
int rtl8735b_dshot_send(const uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS], uint32_t active,
                        const rtl8735b_dshot_timing_t *timing);
// 拉低所有接上的腳，之後 rtl8735b_dshot_send 一律回傳 ESTOPPED（AP_HAL::panic 用）
void rtl8735b_dshot_emergency_stop(void);

#ifdef __cplusplus
}
#endif
