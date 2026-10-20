/* SPDX-License-Identifier: GPL-3.0-or-later */
// dshot_shim.c 的主機測試：GPIO 暫存器與 CPU 週期計數器換成替身，記下每次寫入的時間與中斷狀態，
// 檢查每個位元的三個邊緣、關中斷、兩幀的間隔、接腳與校正的錯誤、緊急停止，以及一幀中途計數器出錯時放棄。
// 替身不是硬體模型：匯流排延遲、快取與中斷抖動要上板量。
// 用法：無參數跑一般情境；send 跑送幀；timing 跑計數器出錯；nocyccnt、notcounting、wrongfreq 各跑一種
// 校正失敗（shim 的狀態是靜態的，每種情境一個行程）。
#include "dshot_frame.h"
#include "dshot_shim.h"

#include <cmsis.h>
#include <hal_gpio.h>
#include <hal_timer.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr uint32_t STEP = 7;   // 每讀一次週期計數器前進的週期數
constexpr uint32_t SLACK = 4 * STEP;

uint32_t make_pin(uint32_t port, uint32_t pin)
{
    return (port << 5) | pin;
}

const uint32_t PIN_E1 = make_pin(4, 1);
const uint32_t PIN_F11 = make_pin(5, 11);
const uint32_t PIN_F12 = make_pin(5, 12);
const uint32_t PIN_F13 = make_pin(5, 13);
const uint32_t PIN_F14 = make_pin(5, 14);
const uint32_t PIN_F15 = make_pin(5, 15);

struct Write {
    volatile uint32_t *reg;
    uint32_t value;
    uint32_t cycles;
    uint32_t primask;
};

uint32_t cycles;
uint32_t cycles_per_us = 500;
bool counting = true;
uint32_t primask;
std::vector<Write> writes;
std::set<uint32_t> busy_pins;
// 每個埠兩個暫存器：0 是「寫 1 輸出低」，1 是「寫 1 輸出高」
volatile uint32_t regs[8][2];
std::vector<std::pair<uint32_t, uint32_t>> pin_writes;   // (腳, 值)
std::vector<uint32_t> out_dirs;
std::vector<uint32_t> deinit_pins;
// 計數器出錯的情境：第幾個真的暫存器寫入（或空跑的寫入）之後停住或跳一次，以及之後讀了幾次計數器
uint32_t real_write_count;
uint32_t dummy_write_count;
uint32_t stall_after;
uint32_t jump_after;
uint32_t dummy_stall_after;
uint32_t dummy_jump_after;
int64_t jump_by;
uint32_t reads_since_fault;

bool is_real(const Write &w)
{
    for (auto &port : regs) {
        if (w.reg == &port[0] || w.reg == &port[1]) {
            return true;
        }
    }
    return false;
}

std::vector<Write> real_writes()
{
    std::vector<Write> out;
    for (const auto &w : writes) {
        if (is_real(w)) {
            out.push_back(w);
        }
    }
    return out;
}

void attach_four()
{
    assert(rtl8735b_dshot_attach(PIN_F14) == 0);
    assert(rtl8735b_dshot_attach(PIN_F11) == 1);
    assert(rtl8735b_dshot_attach(PIN_F12) == 2);
    assert(rtl8735b_dshot_attach(PIN_F13) == 3);
}

void check_frame(const std::vector<Write> &w, size_t first, const uint16_t frames[4], uint32_t active,
                 const rtl8735b_dshot_timing_t &t)
{
    const uint32_t pins[4] = {1u << 14, 1u << 11, 1u << 12, 1u << 13};
    uint32_t all = 0;
    for (int i = 0; i < 4; i++) {
        if (active & (1u << i)) {
            all |= pins[i];
        }
    }
    volatile uint32_t *low = &regs[5][0];
    volatile uint32_t *high = &regs[5][1];
    assert(w.size() >= first + 48);
    const uint32_t start = w[first].cycles;
    for (uint32_t bit = 0; bit < 16; bit++) {
        uint32_t zeros = 0;
        for (int i = 0; i < 4; i++) {
            if ((active & (1u << i)) && (frames[i] & (0x8000u >> bit)) == 0) {
                zeros |= pins[i];
            }
        }
        const Write &rise = w[first + 3 * bit];
        const Write &fall0 = w[first + 3 * bit + 1];
        const Write &fall1 = w[first + 3 * bit + 2];
        assert(rise.reg == high && rise.value == all);
        assert(fall0.reg == low && fall0.value == zeros);
        assert(fall1.reg == low && fall1.value == all);
        // 每個邊緣在目標時間之後、幾次讀取之內
        const uint32_t bit_start = rise.cycles - start;
        assert(bit_start + SLACK >= bit * t.bit_cycles && bit_start <= bit * t.bit_cycles + SLACK);
        const uint32_t h0 = fall0.cycles - rise.cycles;
        const uint32_t h1 = fall1.cycles - rise.cycles;
        assert(h0 + SLACK >= t.t0h_cycles && h0 <= t.t0h_cycles + SLACK);
        assert(h1 + SLACK >= t.t1h_cycles && h1 <= t.t1h_cycles + SLACK);
        // 一幀期間中斷關著
        assert(rise.primask == 1 && fall0.primask == 1 && fall1.primask == 1);
    }
}

void test_attach()
{
    attach_four();
    assert(rtl8735b_dshot_cpu_hz() == 500000000u);
    // 校正時開了週期計數器
    assert((fake_coredebug.DEMCR & CoreDebug_DEMCR_TRCENA_Msk) != 0);
    assert((fake_dwt.CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0);
    // 同一支腳回傳同一個槽位
    assert(rtl8735b_dshot_attach(PIN_F12) == 2);
    // 每支腳設成輸出、先輸出低電位
    assert(out_dirs.size() == 4);
    assert(pin_writes.size() == 4);
    for (const auto &pw : pin_writes) {
        assert(pw.second == 0);
    }
    // 不同的埠不收
    assert(rtl8735b_dshot_attach(PIN_E1) == RTL8735B_DSHOT_EPORT);
    // 不能設成 GPIO 的腳
    busy_pins.insert(PIN_F15);
    assert(rtl8735b_dshot_attach(PIN_F15) == RTL8735B_DSHOT_EGPIO);
    busy_pins.clear();
    // 槽位上限
    uint32_t extra = 0;
    for (uint32_t pin = 0; pin < 32 && extra < RTL8735B_DSHOT_MAX_SLOTS - 4; pin++) {
        if (pin >= 11 && pin <= 14) {
            continue;
        }
        assert(rtl8735b_dshot_attach(make_pin(5, pin)) >= 0);
        extra++;
    }
    assert(rtl8735b_dshot_attach(make_pin(5, 30)) == RTL8735B_DSHOT_EINVAL);
}

void test_send_and_gap()
{
    attach_four();
    rtl8735b_dshot_timing_t t;
    assert(rtl8735b_dshot_timing(300, 0, rtl8735b_dshot_cpu_hz(), &t) == 0);
    uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS] = {0xFFFFu, 0x0000u, 0x8001u, 0x82C6u};
    writes.clear();
    primask = 0;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_OK);
    std::vector<Write> w = real_writes();
    assert(w.size() == 48);
    check_frame(w, 0, frames, 0xFu, t);
    // 先空跑一次（寫到假的位址）讓快取熱起來，不碰真的暫存器
    assert(writes.size() > w.size());
    // 中斷狀態還原
    assert(primask == 0);

    // 只送兩路：其他接上的腳不拉高
    writes.clear();
    assert(rtl8735b_dshot_send(frames, 0x5u, &t) == RTL8735B_DSHOT_OK);
    std::vector<Write> w2 = real_writes();
    assert(w2.size() == 48);
    check_frame(w2, 0, frames, 0x5u, t);
    // 兩幀的間隔至少 40 µs：第二幀的第一個邊緣距第一幀的最後一個邊緣
    const uint32_t gap = w2.front().cycles - w.back().cycles;
    assert(gap >= RTL8735B_DSHOT_MIN_GAP_US * 500u);

    // 呼叫時中斷本來就關著：送完維持關著
    writes.clear();
    primask = 1;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_OK);
    assert(primask == 1);
    primask = 0;

    // 參數錯誤：不寫暫存器
    writes.clear();
    assert(rtl8735b_dshot_send(frames, 0xFu, nullptr) == RTL8735B_DSHOT_EINVAL);
    assert(rtl8735b_dshot_send(nullptr, 0xFu, &t) == RTL8735B_DSHOT_EINVAL);
    assert(rtl8735b_dshot_send(frames, 1u << 4, &t) == RTL8735B_DSHOT_EINVAL);
    assert(real_writes().empty());
    // 沒有要送的槽位：不做事
    assert(rtl8735b_dshot_send(frames, 0, &t) == RTL8735B_DSHOT_OK);
    assert(real_writes().empty());

    // DShot600 與 150 也照時序送
    for (uint16_t rate : {150, 600}) {
        assert(rtl8735b_dshot_timing(rate, 0, rtl8735b_dshot_cpu_hz(), &t) == 0);
        writes.clear();
        assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_OK);
        std::vector<Write> w3 = real_writes();
        check_frame(w3, 0, frames, 0xFu, t);
    }

    // 緊急停止：拉低所有接上的腳，之後不再送
    writes.clear();
    rtl8735b_dshot_emergency_stop();
    std::vector<Write> w4 = real_writes();
    assert(w4.size() == 1 && w4[0].reg == &regs[5][0] && w4[0].value == 0x7800u);
    writes.clear();
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_ESTOPPED);
    assert(real_writes().empty());
    assert(rtl8735b_dshot_attach(PIN_F15) == RTL8735B_DSHOT_ESTOPPED);
}

void test_send_before_attach()
{
    rtl8735b_dshot_timing_t t;
    assert(rtl8735b_dshot_timing(300, 0, 500000000u, &t) == 0);
    uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS] = {};
    assert(rtl8735b_dshot_send(frames, 1u, &t) == RTL8735B_DSHOT_EINVAL);
    assert(rtl8735b_dshot_cpu_hz() == 0);
}

// 一幀送到一半，週期計數器停住、往前跳太遠或倒退：在有限次讀取內放棄這一幀，腳全部拉低、中斷恢復、計數；
// 計數器恢復後下一幀照常。小幅的延遲照送
void test_timing_loss()
{
    attach_four();
    rtl8735b_dshot_timing_t t;
    assert(rtl8735b_dshot_timing(300, 0, rtl8735b_dshot_cpu_hz(), &t) == 0);
    const uint32_t late_limit = (t.t1h_cycles - t.t0h_cycles) / 2;
    uint16_t frames[RTL8735B_DSHOT_MAX_SLOTS] = {0xFFFFu, 0x0000u, 0x8001u, 0x82C6u};
    volatile uint32_t *low = &regs[5][0];
    assert(rtl8735b_dshot_dropped_frames() == 0);

    // 停住：第 10 個寫入（第 3 位元拉高）之後計數器不再前進
    writes.clear();
    real_write_count = 0;
    stall_after = 10;
    primask = 0;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_ETIMING);
    std::vector<Write> w = real_writes();
    assert(w.size() == 11);
    assert(w.back().reg == low && w.back().value == 0x7800u);
    assert(primask == 0);
    assert(reads_since_fault <= 2 * t.bit_cycles);
    assert(rtl8735b_dshot_dropped_frames() == 1);
    // 計數器恢復：下一幀完整送出
    counting = true;
    stall_after = 0;
    writes.clear();
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_OK);
    check_frame(real_writes(), 0, frames, 0xFu, t);

    // 往前跳太遠：第 16 個寫入（第 5 位元拉高）之後跳過 t0h 再超過上限，拉低 0 的邊緣晚太多
    writes.clear();
    real_write_count = 0;
    jump_after = 16;
    jump_by = t.t0h_cycles + late_limit + 50;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_ETIMING);
    w = real_writes();
    assert(w.size() == 17);
    assert(w.back().reg == low && w.back().value == 0x7800u);
    assert(primask == 0);
    assert(rtl8735b_dshot_dropped_frames() == 2);

    // 1 位元的下降邊緣晚上限的一半（第 17 個寫入＝第 5 位元拉低 0 之後跳）：照送完整的一幀
    writes.clear();
    real_write_count = 0;
    jump_after = 17;
    jump_by = (t.t1h_cycles - t.t0h_cycles) + late_limit / 2;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_OK);
    assert(real_writes().size() == 48);
    assert(rtl8735b_dshot_dropped_frames() == 2);

    // 倒退：第 20 個寫入之後計數器回到很早的值
    writes.clear();
    real_write_count = 0;
    jump_after = 20;
    jump_by = -(int64_t)(1u << 20);
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_ETIMING);
    w = real_writes();
    assert(w.size() == 21);
    assert(w.back().reg == low && w.back().value == 0x7800u);
    assert(reads_since_fault <= 2 * t.bit_cycles);
    assert(primask == 0);
    assert(rtl8735b_dshot_dropped_frames() == 3);
    jump_after = 0;

    // 空跑晚到（快取未命中就是這樣）：不放棄，真的那一幀照送
    writes.clear();
    dummy_write_count = 0;
    dummy_jump_after = 1;
    jump_by = t.t0h_cycles + late_limit + 50;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_OK);
    check_frame(real_writes(), 0, frames, 0xFu, t);
    assert(rtl8735b_dshot_dropped_frames() == 3);
    dummy_jump_after = 0;
    // 空跑時計數器停住：讀取次數的上限照樣放棄，不碰真的腳
    writes.clear();
    dummy_write_count = 0;
    dummy_stall_after = 1;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_ETIMING);
    assert(real_writes().empty());
    assert(reads_since_fault <= 2 * t.bit_cycles);
    assert(primask == 0);
    assert(rtl8735b_dshot_dropped_frames() == 4);
    counting = true;
    dummy_stall_after = 0;

    // 呼叫時中斷本來就關著：放棄後維持關著
    writes.clear();
    real_write_count = 0;
    stall_after = 10;
    primask = 1;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_ETIMING);
    assert(primask == 1);
    assert(rtl8735b_dshot_dropped_frames() == 5);
    primask = 0;
    counting = true;
    stall_after = 0;

    // 0 位元的下降邊緣晚 40 週期（80 ns）：遠小於 (T1H−T0H)/2，但電調的 0／1 分界只比 0 位元多約 60 ns，
    // 有通道送 0 的位元就放棄
    writes.clear();
    real_write_count = 0;
    jump_after = 16;
    jump_by = t.t0h_cycles + 40;
    assert(rtl8735b_dshot_send(frames, 0xFu, &t) == RTL8735B_DSHOT_ETIMING);
    w = real_writes();
    assert(w.size() == 17);
    assert(w.back().reg == low && w.back().value == 0x7800u);
    assert(rtl8735b_dshot_dropped_frames() == 6);
    // 同樣的延遲落在沒有通道送 0 的位元（只送全 1 的那一路）：那個邊緣不改變任何腳，照送
    writes.clear();
    real_write_count = 0;
    assert(rtl8735b_dshot_send(frames, 0x1u, &t) == RTL8735B_DSHOT_OK);
    assert(real_writes().size() == 48);
    assert(rtl8735b_dshot_dropped_frames() == 6);
    jump_after = 0;
}

void test_calibration_failure()
{
    assert(rtl8735b_dshot_attach(PIN_F11) == RTL8735B_DSHOT_ECLOCK);
    assert(rtl8735b_dshot_cpu_hz() == 0);
    // 之後每次都失敗，不會重新校正成功
    counting = true;
    cycles_per_us = 500;
    fake_dwt.CTRL &= ~DWT_CTRL_NOCYCCNT_Msk;
    assert(rtl8735b_dshot_attach(PIN_F11) == RTL8735B_DSHOT_ECLOCK);
    assert(out_dirs.empty() && pin_writes.empty());
}

}  // namespace

extern "C" {
DWT_Type fake_dwt;
CoreDebug_Type fake_coredebug;

uint32_t __get_PRIMASK(void)
{
    return primask;
}

void __set_PRIMASK(uint32_t value)
{
    primask = value;
}

void __disable_irq(void)
{
    primask = 1;
}

uint32_t fake_cycles_read(void)
{
    reads_since_fault++;
    if (counting) {
        cycles += STEP;
    }
    return cycles;
}

void fake_reg_write(volatile uint32_t *reg, uint32_t value)
{
    const Write w{reg, value, cycles, primask};
    writes.push_back(w);
    *reg = value;
    if (!is_real(w)) {
        dummy_write_count++;
        if (dummy_stall_after != 0 && dummy_write_count == dummy_stall_after) {
            counting = false;
            reads_since_fault = 0;
        }
        if (dummy_jump_after != 0 && dummy_write_count == dummy_jump_after) {
            cycles = (uint32_t)((int64_t)cycles + jump_by);
        }
        return;
    }
    real_write_count++;
    if (stall_after != 0 && real_write_count == stall_after) {
        counting = false;
        reads_since_fault = 0;
    }
    if (jump_after != 0 && real_write_count == jump_after) {
        cycles = (uint32_t)((int64_t)cycles + jump_by);
        reads_since_fault = 0;
    }
}

uint32_t hal_read_curtime_us(void)
{
    // 時間也跟著讀取前進，讓等待間隔的迴圈會結束
    cycles += STEP;
    return cycles / cycles_per_us;
}

void hal_delay_us(uint32_t time_us)
{
    if (counting) {
        cycles += time_us * cycles_per_us;
    }
}

hal_status_t hal_gpio_init(phal_gpio_adapter_t a, uint32_t pin_name)
{
    if (busy_pins.count(pin_name)) {
        return HAL_ERR_HW;
    }
    const uint32_t port = pin_name >> 5;
    memset(a, 0, sizeof(*a));
    a->pin_name = (uint8_t)pin_name;
    a->port_idx = (uint8_t)port;
    a->pin_idx = (uint8_t)(pin_name & 31u);
    a->bit_mask = 1u << (pin_name & 31u);
    a->out0_port = &regs[port][0];
    a->out1_port = &regs[port][1];
    return HAL_OK;
}

void hal_gpio_deinit(phal_gpio_adapter_t a)
{
    deinit_pins.push_back(a->pin_name);
}

void hal_gpio_set_dir(phal_gpio_adapter_t a, gpio_dir_t dir)
{
    assert(dir == GPIO_OUT);
    out_dirs.push_back(a->pin_name);
}

void hal_gpio_write(phal_gpio_adapter_t a, uint32_t io_data)
{
    pin_writes.emplace_back(a->pin_name, io_data);
}
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "nocyccnt") {
        fake_dwt.CTRL = DWT_CTRL_NOCYCCNT_Msk;
        test_calibration_failure();
    } else if (mode == "notcounting") {
        counting = false;
        test_calibration_failure();
    } else if (mode == "wrongfreq") {
        // CPU 只跑 400 MHz
        cycles_per_us = 400;
        test_calibration_failure();
    } else if (mode == "send") {
        test_send_before_attach();
        test_send_and_gap();
    } else if (mode == "timing") {
        test_timing_loss();
    } else {
        test_attach();
    }
    printf("dshot shim%s%s: all tests passed\n", mode.empty() ? "" : " ", mode.c_str());
    return 0;
}
