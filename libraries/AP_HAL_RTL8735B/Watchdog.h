/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

namespace RTL8735B { class Watchdog; }

/*
  硬體看門狗的啟動、餵狗與「預期中的長延遲」判斷：setup()
  之後依 BRD_OPTIONS bit0 啟動、主迴圈每圈餵狗；Scheduler.cpp 的 expect_delay_ms／in_expected_delay，
  以及 timer 執行緒在預期延遲期間代為餵狗。這裡不呼叫 FreeRTOS，時間由呼叫端傳入，主機可測。
 */
class RTL8735B::Watchdog
{
public:
    // SDK 實際換算出的逾時待板上量測（wdt_shim.c）。
    static const uint32_t TIMEOUT_MS = 2048;

    // enabled 為 false 時不碰硬體，看門狗維持關閉。只啟動一次。
    void start(bool enabled);
    bool started() const { return _started; }
    // 已啟動才真的餵狗；last_pat_ms 一律更新
    void pat(uint32_t now_ms);
    uint32_t last_pat_ms() const { return _last_pat_ms; }

    // 主執行緒呼叫；ms 為 0 時取消一層（巢狀計數）。
    void expect_delay(uint32_t now_ms, uint32_t ms);
    bool in_expected_delay(uint32_t now_ms) const;

private:
    volatile bool _started = false;
    volatile uint32_t _last_pat_ms = 0;
    // 主執行緒寫、timer 執行緒讀；不加鎖（32 位元讀寫不會撕裂）
    volatile uint32_t _expect_delay_start = 0;
    volatile uint32_t _expect_delay_length = 0;
    uint32_t _expect_delay_nesting = 0;
};
