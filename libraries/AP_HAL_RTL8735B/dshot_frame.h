/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * DShot 的幀與時序計算，不碰 SDK（送幀在 dshot_shim.c）。
 *
 * 幀：16 位元，高位元先送 = 11 位元數值、1 位元遙測請求、4 位元檢查碼；數值 0 是停止，1–47 是命令，
 * 48–2047 是油門。位元的高電位時間照 ArduPilot（libraries/AP_HAL/RCOutput.h 的 DSHOT_BIT_WIDTH_TICKS 等）：
 * 一般 8 格裡 0 佔 3 格、1 佔 6 格；BLHeli_S 11 格裡 0 佔 4 格、1 佔 8 格。
 */

#include <stdint.h>

#define RTL8735B_DSHOT_FRAME_BITS 16u
#define RTL8735B_DSHOT_MAX_VALUE 2047u
// 油門的最小值；0 是停止，1–47 是命令
#define RTL8735B_DSHOT_MIN_THROTTLE 48u

typedef struct {
    uint32_t bit_cycles;    // 位元週期（CPU 週期）
    uint32_t t0h_cycles;    // 0 的高電位時間
    uint32_t t1h_cycles;    // 1 的高電位時間
} rtl8735b_dshot_timing_t;

#ifdef __cplusplus
extern "C" {
#endif

// value 只取低 11 位元；telem 非 0 時設遙測請求位元
uint16_t rtl8735b_dshot_frame(uint16_t value, uint8_t telem);

// 脈寬（µs）換成 DShot 數值：未解鎖或脈寬 ≤ 1000 時為 0（停止）；否則夾在 1000–2000、
// 2×(脈寬−1000) 上限 1999，再加 48。同 ArduPilot ChibiOS 的 RCOutput::dshot_send。
uint16_t rtl8735b_dshot_throttle_value(uint16_t pulse_us, uint8_t armed);

// rate_khz 只接受 150、300、600；wide_bits 非 0 時用 BLHeli_S 的位寬。位元週期取整數部分
// （速率不低於標稱），高電位時間依精確的位元週期四捨五入。不支援時回傳 -1，*out 不動。
int rtl8735b_dshot_timing(uint16_t rate_khz, uint8_t wide_bits, uint32_t cpu_hz, rtl8735b_dshot_timing_t *out);

// 每個位元（索引 0 是最先送的最高位元）要在 0 的高電位時間結束時拉低的腳：該位元為 0 的通道的
// 埠遮罩之和。frames[i] 對應 pin_masks[i]。
void rtl8735b_dshot_zero_masks(const uint16_t *frames, const uint32_t *pin_masks, uint8_t count,
                               uint32_t zero_masks[RTL8735B_DSHOT_FRAME_BITS]);

#ifdef __cplusplus
}
#endif
