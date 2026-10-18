/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * DShot 的幀與時序計算，介面見 dshot_frame.h。
 * 幀的組法（數值左移一位加遙測位元，檢查碼為三個半位元組互斥或）與油門換算照 ArduPilot 9f648ccabc 的
 * libraries/AP_HAL_ChibiOS/RCOutput.cpp（create_dshot_packet、dshot_send）。
 */

#include "dshot_frame.h"

#include <stddef.h>

uint16_t rtl8735b_dshot_frame(uint16_t value, uint8_t telem)
{
    const uint16_t packet = (uint16_t)(((value & RTL8735B_DSHOT_MAX_VALUE) << 1) | (telem ? 1u : 0u));
    const uint16_t csum = (uint16_t)((packet ^ (packet >> 4) ^ (packet >> 8)) & 0xFu);
    return (uint16_t)((packet << 4) | csum);
}

uint16_t rtl8735b_dshot_throttle_value(uint16_t pulse_us, uint8_t armed)
{
    if (!armed || pulse_us <= 1000u) {
        return 0;
    }
    const uint32_t pwm = pulse_us > 2000u ? 2000u : pulse_us;
    uint32_t value = 2u * (pwm - 1000u);
    if (value > 1999u) {
        value = 1999u;
    }
    return (uint16_t)(value + RTL8735B_DSHOT_MIN_THROTTLE);
}

// 精確的高電位時間（CPU 週期）四捨五入：cpu_hz × high / (rate_hz × width)
static uint32_t _high_cycles(uint32_t cpu_hz, uint32_t rate_hz, uint32_t high_ticks, uint32_t width_ticks)
{
    const uint64_t den = (uint64_t)rate_hz * width_ticks;
    return (uint32_t)(((uint64_t)cpu_hz * high_ticks + den / 2u) / den);
}

int rtl8735b_dshot_timing(uint16_t rate_khz, uint8_t wide_bits, uint32_t cpu_hz, rtl8735b_dshot_timing_t *out)
{
    if (out == NULL || (rate_khz != 150u && rate_khz != 300u && rate_khz != 600u)) {
        return -1;
    }
    const uint32_t rate_hz = (uint32_t)rate_khz * 1000u;
    // ArduPilot libraries/AP_HAL/RCOutput.h：一般 8/3/6 格，BLHeli_S 11/4/8 格
    const uint32_t width = wide_bits ? 11u : 8u;
    const uint32_t high0 = wide_bits ? 4u : 3u;
    const uint32_t high1 = wide_bits ? 8u : 6u;
    rtl8735b_dshot_timing_t t;
    t.bit_cycles = cpu_hz / rate_hz;
    t.t0h_cycles = _high_cycles(cpu_hz, rate_hz, high0, width);
    t.t1h_cycles = _high_cycles(cpu_hz, rate_hz, high1, width);
    if (t.t0h_cycles == 0 || t.t0h_cycles >= t.t1h_cycles || t.t1h_cycles >= t.bit_cycles) {
        return -1;
    }
    *out = t;
    return 0;
}

void rtl8735b_dshot_zero_masks(const uint16_t *frames, const uint32_t *pin_masks, uint8_t count,
                               uint32_t zero_masks[RTL8735B_DSHOT_FRAME_BITS])
{
    for (uint32_t bit = 0; bit < RTL8735B_DSHOT_FRAME_BITS; bit++) {
        const uint16_t sel = (uint16_t)(0x8000u >> bit);
        uint32_t mask = 0;
        for (uint8_t ch = 0; ch < count; ch++) {
            if ((frames[ch] & sel) == 0) {
                mask |= pin_masks[ch];
            }
        }
        zero_masks[bit] = mask;
    }
}
