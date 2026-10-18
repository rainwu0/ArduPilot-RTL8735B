/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

namespace RTL8735B {

/*
 * 主迴圈最近一次跑完的時刻，給開發板的應用程式查詢（app/rtl8735b_app.h 的
 * rtl8735b_ardupilot_ms_since_main_loop()）。
 * 只由主 task 寫，任何 task 讀；32 位元的對齊讀寫在 Cortex-M 上是單一指令，不必上鎖。
 */
class MainLoopClock {
public:
    // 主 task 每圈主迴圈跑完時呼叫。先寫時刻、再設旗標：讀到旗標為真時時刻已經有效
    void mark(uint32_t now_ms)
    {
        _last_ms = now_ms;
        _ran = true;
    }

    // 主迴圈還沒跑完過一圈時回傳 UINT32_MAX。先讀主迴圈的時刻、再讀現在：
    // 兩次讀之間主 task 剛好更新，結果仍是小的正數，不會變成負數繞成極大值
    template <typename NowMs>
    uint32_t ms_since(NowMs now_ms) const
    {
        if (!_ran) {
            return UINT32_MAX;
        }
        const uint32_t last = _last_ms;
        return now_ms() - last;
    }

private:
    volatile uint32_t _last_ms = 0;
    volatile bool _ran = false;
};

} // namespace RTL8735B
