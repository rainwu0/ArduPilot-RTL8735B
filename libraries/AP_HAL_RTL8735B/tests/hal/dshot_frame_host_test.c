/* SPDX-License-Identifier: GPL-3.0-or-later */
// dshot_frame.c 的主機測試：幀與檢查碼、油門換算、各速率的時序、每個位元要拉低的腳。
#include "dshot_frame.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_frame(void)
{
    // 公開的範例：油門 1046、不要遙測 → 0x82C6
    assert(rtl8735b_dshot_frame(1046, 0) == 0x82C6);
    // 遙測位元是幀的第 4 位元；檢查碼跟著變
    const uint16_t with_telem = rtl8735b_dshot_frame(1046, 1);
    assert((with_telem & 0xFFF0u) == ((0x82C6u & 0xFFF0u) | 0x0010u));
    assert((with_telem & 0x000Fu) != 0x6u);
    // 檢查碼：12 位元的三個半位元組互斥或
    for (uint32_t value = 0; value <= RTL8735B_DSHOT_MAX_VALUE; value += 7) {
        for (uint8_t telem = 0; telem < 2; telem++) {
            const uint16_t frame = rtl8735b_dshot_frame((uint16_t)value, telem);
            const uint16_t packet = (uint16_t)(frame >> 4);
            assert(packet == ((value << 1) | telem));
            assert((frame & 0xFu) == ((packet ^ (packet >> 4) ^ (packet >> 8)) & 0xFu));
        }
    }
    // 停止與最大油門
    assert(rtl8735b_dshot_frame(0, 0) == 0x0000);
    assert((rtl8735b_dshot_frame(2047, 0) >> 5) == 2047);
    // 超過 11 位元的部分不會溢到遙測位元
    assert((rtl8735b_dshot_frame(0x0800u | 5u, 0) >> 4) == (5u << 1));
}

static void test_throttle(void)
{
    // 未解鎖一律停止
    assert(rtl8735b_dshot_throttle_value(1500, 0) == 0);
    assert(rtl8735b_dshot_throttle_value(2000, 0) == 0);
    // 脈寬 ≤ 1000 是停止
    assert(rtl8735b_dshot_throttle_value(0, 1) == 0);
    assert(rtl8735b_dshot_throttle_value(900, 1) == 0);
    assert(rtl8735b_dshot_throttle_value(1000, 1) == 0);
    // 1001 是最小油門 48 + 2
    assert(rtl8735b_dshot_throttle_value(1001, 1) == 50);
    assert(rtl8735b_dshot_throttle_value(1500, 1) == 1048);
    // 上限 1999 + 48 = 2047；超過 2000 夾住
    assert(rtl8735b_dshot_throttle_value(2000, 1) == 2047);
    assert(rtl8735b_dshot_throttle_value(2500, 1) == 2047);
    // 永遠不落在命令的範圍 1–47
    for (uint32_t pwm = 0; pwm <= 2200; pwm++) {
        const uint16_t value = rtl8735b_dshot_throttle_value((uint16_t)pwm, 1);
        assert(value == 0 || (value >= RTL8735B_DSHOT_MIN_THROTTLE && value <= RTL8735B_DSHOT_MAX_VALUE));
    }
}

static void test_timing(void)
{
    rtl8735b_dshot_timing_t t;
    const uint32_t cpu = 500000000u;
    // 一般位寬 8 格、0 佔 3 格、1 佔 6 格（ArduPilot 的 DSHOT_BIT_0_TICKS／DSHOT_BIT_1_TICKS）
    assert(rtl8735b_dshot_timing(150, 0, cpu, &t) == 0);
    assert(t.bit_cycles == 3333 && t.t0h_cycles == 1250 && t.t1h_cycles == 2500);
    assert(rtl8735b_dshot_timing(300, 0, cpu, &t) == 0);
    assert(t.bit_cycles == 1666 && t.t0h_cycles == 625 && t.t1h_cycles == 1250);
    assert(rtl8735b_dshot_timing(600, 0, cpu, &t) == 0);
    assert(t.bit_cycles == 833 && t.t0h_cycles == 313 && t.t1h_cycles == 625);
    // BLHeli_S：11 格、0 佔 4 格、1 佔 8 格
    assert(rtl8735b_dshot_timing(300, 1, cpu, &t) == 0);
    assert(t.bit_cycles == 1666 && t.t0h_cycles == 606 && t.t1h_cycles == 1212);
    assert(rtl8735b_dshot_timing(600, 1, cpu, &t) == 0);
    assert(t.bit_cycles == 833 && t.t0h_cycles == 303 && t.t1h_cycles == 606);
    // 高電位一定比位元週期短，1 比 0 長
    for (uint16_t rate = 150; rate <= 600; rate *= 2) {
        for (uint8_t wide = 0; wide < 2; wide++) {
            assert(rtl8735b_dshot_timing(rate, wide, cpu, &t) == 0);
            assert(t.t0h_cycles < t.t1h_cycles && t.t1h_cycles < t.bit_cycles);
        }
    }
    // 不支援的速率：*out 不動
    t.bit_cycles = 123;
    assert(rtl8735b_dshot_timing(1200, 0, cpu, &t) == -1);
    assert(rtl8735b_dshot_timing(0, 0, cpu, &t) == -1);
    assert(rtl8735b_dshot_timing(301, 0, cpu, &t) == -1);
    assert(t.bit_cycles == 123);
    assert(rtl8735b_dshot_timing(300, 0, cpu, NULL) == -1);
    // CPU 太慢時位元太短，不支援
    assert(rtl8735b_dshot_timing(600, 0, 1000000u, &t) == -1);
}

static void test_zero_masks(void)
{
    // 四路：PF11–PF14 的埠遮罩
    const uint32_t pins[4] = {1u << 14, 1u << 11, 1u << 12, 1u << 13};
    const uint16_t frames[4] = {0xFFFFu, 0x0000u, 0x8001u, 0x82C6u};
    uint32_t zeros[RTL8735B_DSHOT_FRAME_BITS];
    rtl8735b_dshot_zero_masks(frames, pins, 4, zeros);
    for (uint32_t bit = 0; bit < RTL8735B_DSHOT_FRAME_BITS; bit++) {
        uint32_t want = 0;
        for (uint32_t ch = 0; ch < 4; ch++) {
            if ((frames[ch] & (0x8000u >> bit)) == 0) {
                want |= pins[ch];
            }
        }
        assert(zeros[bit] == want);
    }
    // 第一個送的位元是最高位元：0x8001 的最高位元是 1，0x0000 是 0
    assert((zeros[0] & (1u << 12)) == 0);
    assert((zeros[0] & (1u << 11)) != 0);
    // 最後一位元：0x8001 為 1、0x82C6 為 0
    assert((zeros[15] & (1u << 12)) == 0);
    assert((zeros[15] & (1u << 13)) != 0);
    // 0 路：全部為 0
    rtl8735b_dshot_zero_masks(frames, pins, 0, zeros);
    for (uint32_t bit = 0; bit < RTL8735B_DSHOT_FRAME_BITS; bit++) {
        assert(zeros[bit] == 0);
    }
}

int main(void)
{
    test_frame();
    test_throttle();
    test_timing();
    test_zero_masks();
    printf("dshot frame: all tests passed\n");
    return 0;
}
