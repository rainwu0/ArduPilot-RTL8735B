/*
 * 給開發板應用程式的主迴圈時刻（AppStatus.h 的 MainLoopClock，app/rtl8735b_app.h 的
 * rtl8735b_ardupilot_ms_since_main_loop()）。
 */

#include "AppStatus.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main()
{
    RTL8735B::MainLoopClock clock;

    // 主迴圈還沒跑完過一圈
    assert(clock.ms_since([] { return uint32_t(5000); }) == UINT32_MAX);

    clock.mark(100);
    assert(clock.ms_since([] { return uint32_t(150); }) == 50);

    // millis() 回繞
    clock.mark(0xFFFFFFF0u);
    assert(clock.ms_since([] { return uint32_t(0x10); }) == 0x20);

    // 先讀主迴圈的時刻、再讀現在：讀「現在」的當下主 task 剛好跑完一圈並更新，結果仍是小的正數，
    // 不會變成負數繞成極大值
    clock.mark(1000);
    const uint32_t gap = clock.ms_since([&clock] {
        const uint32_t now = 1004;
        clock.mark(1005);
        return now;
    });
    assert(gap == 4);

    puts("PASS: main loop clock for board applications");
    return 0;
}
