/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 以 C 編譯的 DShot 送幀，介面見 dshot_shim.h。
 *
 * GPIO：SDK 的單腳 GPIO adapter（hal_gpio.h 的 hal_gpio_init）給出該腳所在埠的「寫 1 輸出低」（out0_port）
 * 與「寫 1 輸出高」（out1_port）暫存器位址（rtl8735b_gpio.h 的 hal_gpio_adapter_t）。同一個晶片埠的腳共用這兩個
 * 暫存器，寫 1 的位元動作、寫 0 的位元不變（rtl8735b_gpio_type.h 的 GPIO_PORT_A_ODL_EN、GPIO_PORT_A_ODH_EN），
 * 一次寫入就能同時拉高或拉低幾支腳。SDK 的單腳寫入看來也用這兩個暫存器（adapter 只給這兩個位址），所以和其他
 * 執行緒寫同一埠的其他腳（例如 LED）不會互相蓋掉；ROM 內的實作看不到，這是推論。
 *
 * 時序：以 CPU 週期計數器（DWT CYCCNT）等到每個邊緣的目標時間再寫，匯流排寫入的固定延遲不影響脈寬。一幀期間
 * 以 PRIMASK 關中斷；安全世界的中斷從這裡關不掉，它造成的抖動要上板量。程式在 DDR、經快取執行，所以先空跑一個
 * 位元（寫到記憶體裡的假暫存器）讓迴圈進快取，再送真的。
 *
 * 計數器出錯時放棄這一幀：每次等待最多讀 bit_cycles + DSHOT_LEAD_CYCLES 次計數器（每讀一次至少花一個週期，
 * 正常的等待不會超過這個次數），計數器停住或倒退時在這個次數內放棄；等到時已經晚了 (t1h − t0h) / 2 以上
 * （計數器往前跳、或被安全世界的中斷拖住）也放棄，因為晚這麼多，電調可能把這個位元的 1 讀成 0。
 * 0 位元的下降邊緣另用緊的上限 DSHOT_ZERO_FALL_LATE_CYCLES：這個邊緣晚了，0 的高電位就變長，而電調的 0／1 分界
 * 只比 0 位元的高電位長一點（Bluejay、BLHeli_S 由原始碼推算約 60 ns，AM32 約 350 ns；推論，待邏輯分析儀實測）。
 * 推算所用的原始碼：Bluejay（bird-sanctuary/bluejay 0368d114）、BLHeli_S Rev16.7（bitdump/BLHeli 80ac909b 的
 * `BLHeli_S SiLabs/BLHeli_S.asm`）、AM32 2.21（am32-firmware/AM32 a47e94c2）。
 * 那一位元沒有通道送 0 時，這個邊緣不改變任何腳，照用一般的上限。
 * 空跑只用讀取次數的上限、不看遲到：空跑本來就是要吃下快取未命中，晚到是預期的，也不碰真的腳。
 * 放棄時把腳全部拉低、恢復中斷，回傳 RTL8735B_DSHOT_ETIMING 並計數；下一幀照常送。
 */

#include "dshot_shim.h"

#include <stddef.h>
#include <stdint.h>

#include <cmsis.h>
#include <hal_gpio.h>
#include <hal_timer.h>

// 主機測試的替身改寫這兩個巨集，記下讀取與寫入的時間
#ifndef RTL8735B_DSHOT_CYCLES
#define RTL8735B_DSHOT_CYCLES() (DWT->CYCCNT)
#endif
#ifndef RTL8735B_DSHOT_REG_WRITE
#define RTL8735B_DSHOT_REG_WRITE(reg, value) (*(reg) = (value))
#endif

// 晶片腳編號的埠（SDK rtl8735b_pin_name.h 的 PIN_NAME_2_PORT）
#define DSHOT_PIN_PORT(pin) (((pin) >> 5) & 0xFu)
// 校正：數 1 ms 內的 CPU 週期，和 CONFIG_CPU_CLK 相差 3% 以內才通過
#define DSHOT_CALIB_US 1000u
#define DSHOT_CALIB_TOLERANCE_PCT 3u
// 從決定起點到第一個邊緣的準備時間（CPU 週期）
#define DSHOT_LEAD_CYCLES 64u
// 0 位元下降邊緣的遲到上限：500 MHz 下 50 ns，比電調推算的 0／1 分界餘裕（約 60 ns）小
#define DSHOT_ZERO_FALL_LATE_CYCLES 25u

enum {
    DSHOT_CALIB_NOT_YET = 0,
    DSHOT_CALIB_OK,
    DSHOT_CALIB_FAILED,
};

static hal_gpio_adapter_t dshot_adapters[RTL8735B_DSHOT_MAX_SLOTS];
static uint32_t dshot_pin_names[RTL8735B_DSHOT_MAX_SLOTS];
static uint32_t dshot_bit_masks[RTL8735B_DSHOT_MAX_SLOTS];
static uint8_t dshot_slots;
static uint32_t dshot_port;
static volatile uint32_t *dshot_low;
static volatile uint32_t *dshot_high;
static uint8_t dshot_calibration;
static uint32_t dshot_cpu_hz_value;
static uint8_t dshot_stopped;
static uint8_t dshot_sent;
static uint32_t dshot_last_end_us;
static uint32_t dshot_dropped_frames;
// 空跑時寫的假暫存器
static volatile uint32_t dshot_dummy_reg;

// 一組相鄰讀取的週期數與微秒；讀取期間關中斷，免得中間被搶走時間
static void _sample(uint32_t *cycles, uint32_t *us)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *cycles = RTL8735B_DSHOT_CYCLES();
    *us = hal_read_curtime_us();
    __set_PRIMASK(primask);
}

static int _calibrate(void)
{
    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0) {
        return RTL8735B_DSHOT_ECLOCK;
    }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t c0, t0, c1, t1;
    _sample(&c0, &t0);
    hal_delay_us(DSHOT_CALIB_US);
    _sample(&c1, &t1);
    const uint32_t dt = t1 - t0;
    if (dt == 0) {
        return RTL8735B_DSHOT_ECLOCK;
    }
    const uint64_t hz = (uint64_t)(c1 - c0) * 1000000u / dt;
    const uint64_t nominal = CONFIG_CPU_CLK;
    const uint64_t tolerance = nominal * DSHOT_CALIB_TOLERANCE_PCT / 100u;
    if (hz + tolerance < nominal || hz > nominal + tolerance) {
        return RTL8735B_DSHOT_ECLOCK;
    }
    dshot_cpu_hz_value = CONFIG_CPU_CLK;
    return RTL8735B_DSHOT_OK;
}

int rtl8735b_dshot_attach(uint32_t pin)
{
    if (dshot_stopped) {
        return RTL8735B_DSHOT_ESTOPPED;
    }
    for (uint8_t i = 0; i < dshot_slots; i++) {
        if (dshot_pin_names[i] == pin) {
            return i;
        }
    }
    if (dshot_calibration == DSHOT_CALIB_NOT_YET) {
        dshot_calibration = _calibrate() == RTL8735B_DSHOT_OK ? DSHOT_CALIB_OK : DSHOT_CALIB_FAILED;
    }
    if (dshot_calibration != DSHOT_CALIB_OK) {
        return RTL8735B_DSHOT_ECLOCK;
    }
    if (dshot_slots >= RTL8735B_DSHOT_MAX_SLOTS) {
        return RTL8735B_DSHOT_EINVAL;
    }
    if (dshot_slots > 0 && DSHOT_PIN_PORT(pin) != dshot_port) {
        return RTL8735B_DSHOT_EPORT;
    }
    hal_gpio_adapter_t *adapter = &dshot_adapters[dshot_slots];
    if (hal_gpio_init(adapter, pin) != HAL_OK) {
        return RTL8735B_DSHOT_EGPIO;
    }
    if (dshot_slots > 0 && (adapter->out0_port != dshot_low || adapter->out1_port != dshot_high)) {
        hal_gpio_deinit(adapter);
        return RTL8735B_DSHOT_EPORT;
    }
    // 先定好輸出值再改方向，切換時不會短暫輸出高電位
    hal_gpio_write(adapter, 0);
    hal_gpio_set_dir(adapter, GPIO_OUT);
    if (dshot_slots == 0) {
        dshot_port = DSHOT_PIN_PORT(pin);
        dshot_low = adapter->out0_port;
        dshot_high = adapter->out1_port;
    }
    dshot_pin_names[dshot_slots] = pin;
    dshot_bit_masks[dshot_slots] = adapter->bit_mask;
    return dshot_slots++;
}

uint32_t rtl8735b_dshot_cpu_hz(void)
{
    return dshot_calibration == DSHOT_CALIB_OK ? dshot_cpu_hz_value : 0;
}

uint32_t rtl8735b_dshot_dropped_frames(void)
{
    return dshot_dropped_frames;
}

// 等到計數器過了 target 回傳 0；讀了 budget 次還沒到（計數器停住或倒退），或到的時候已經晚超過 late_limit
// 個週期（計數器往前跳，或被拖住），回傳 -1。內嵌在送位元的迴圈裡：讀到計數器過了目標到寫入之間的指令越少，
// 邊緣越準
static inline __attribute__((always_inline)) int _wait_until(uint32_t target, uint32_t budget, uint32_t late_limit)
{
    int32_t late;
    while ((late = (int32_t)(RTL8735B_DSHOT_CYCLES() - target)) < 0) {
        if (--budget == 0) {
            return -1;
        }
    }
    return (uint32_t)late > late_limit ? -1 : 0;
}

// 每個位元：起點全部拉高，0 的高電位時間到時拉低送 0 的腳，1 的高電位時間到時拉低其餘的腳。
// 計數器出錯時（見檔頭）拉低所有的腳、回傳 -1。late_limit 是一般邊緣的遲到上限，zero_late_limit 是有通道送 0
// 的位元裡 0 的下降邊緣的上限；空跑兩個都給 UINT32_MAX（不看遲到）。
// 不內嵌、不特化：空跑與真的送幀要跑同一份程式碼，空跑才能把它載入快取（韌體的編譯器在 -Os 會把兩處呼叫
// 各內嵌一份；只禁止內嵌時，gcc 仍可能依兩處不同的常數參數各做一份特化）
static __attribute__((noinline, noclone)) int _send_bits(volatile uint32_t *low, volatile uint32_t *high, uint32_t all,
                       const uint32_t zeros[RTL8735B_DSHOT_FRAME_BITS], uint32_t bits,
                       const rtl8735b_dshot_timing_t *timing, uint32_t late_limit, uint32_t zero_late_limit)
{
    const uint32_t budget = timing->bit_cycles + DSHOT_LEAD_CYCLES;
    uint32_t start = RTL8735B_DSHOT_CYCLES() + DSHOT_LEAD_CYCLES;
    for (uint32_t bit = 0; bit < bits; bit++) {
        if (_wait_until(start, budget, late_limit) != 0) {
            goto lost;
        }
        RTL8735B_DSHOT_REG_WRITE(high, all);
        if (_wait_until(start + timing->t0h_cycles, budget,
                        zeros[bit] != 0 ? zero_late_limit : late_limit) != 0) {
            goto lost;
        }
        RTL8735B_DSHOT_REG_WRITE(low, zeros[bit]);
        if (_wait_until(start + timing->t1h_cycles, budget, late_limit) != 0) {
            goto lost;
        }
        RTL8735B_DSHOT_REG_WRITE(low, all);
        start += timing->bit_cycles;
    }
    return 0;
lost:
    RTL8735B_DSHOT_REG_WRITE(low, all);
    return -1;
}

int rtl8735b_dshot_send(const uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS], uint32_t active,
                        const rtl8735b_dshot_timing_t *timing)
{
    if (dshot_stopped) {
        return RTL8735B_DSHOT_ESTOPPED;
    }
    if (frames == NULL || timing == NULL || (active & ~((1u << dshot_slots) - 1u)) != 0) {
        return RTL8735B_DSHOT_EINVAL;
    }
    if (active == 0) {
        return RTL8735B_DSHOT_OK;
    }
    uint16_t active_frames[RTL8735B_DSHOT_MAX_SLOTS];
    uint32_t active_masks[RTL8735B_DSHOT_MAX_SLOTS];
    uint8_t count = 0;
    uint32_t all = 0;
    for (uint8_t i = 0; i < dshot_slots; i++) {
        if ((active & (1u << i)) != 0) {
            active_frames[count] = frames[i];
            active_masks[count] = dshot_bit_masks[i];
            all |= dshot_bit_masks[i];
            count++;
        }
    }
    uint32_t zeros[RTL8735B_DSHOT_FRAME_BITS];
    rtl8735b_dshot_zero_masks(active_frames, active_masks, count, zeros);
    if (dshot_sent) {
        while ((uint32_t)(hal_read_curtime_us() - dshot_last_end_us) < RTL8735B_DSHOT_MIN_GAP_US) {
        }
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    int lost = _send_bits(&dshot_dummy_reg, &dshot_dummy_reg, all, zeros, 1, timing, UINT32_MAX, UINT32_MAX);
    if (lost == 0) {
        lost = _send_bits(dshot_low, dshot_high, all, zeros, RTL8735B_DSHOT_FRAME_BITS, timing,
                          (timing->t1h_cycles - timing->t0h_cycles) / 2u, DSHOT_ZERO_FALL_LATE_CYCLES);
    }
    __set_PRIMASK(primask);
    dshot_last_end_us = hal_read_curtime_us();
    dshot_sent = 1;
    if (lost != 0) {
        dshot_dropped_frames++;
        return RTL8735B_DSHOT_ETIMING;
    }
    return RTL8735B_DSHOT_OK;
}

void rtl8735b_dshot_emergency_stop(void)
{
    dshot_stopped = 1;
    uint32_t all = 0;
    for (uint8_t i = 0; i < dshot_slots; i++) {
        all |= dshot_bit_masks[i];
    }
    if (all != 0) {
        RTL8735B_DSHOT_REG_WRITE(dshot_low, all);
    }
}
