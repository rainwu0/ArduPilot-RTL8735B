// Watchdog（硬體看門狗的啟動條件、餵狗與預期延遲）的主機測試；wdt_shim 以替身取代。
// 主迴圈每圈呼叫 watchdog_pat() 的接線在 HAL_RTL8735B::run()，不在這裡測（以 ARM 建置與反組譯核對）。
#include "Watchdog.h"
#include "wdt_shim.h"

#include <assert.h>
#include <stdint.h>

namespace {
unsigned start_calls;
uint32_t start_timeout_ms;
int start_result;
unsigned refresh_calls;

void reset_fake()
{
    start_calls = 0;
    start_timeout_ms = 0;
    start_result = 0;
    refresh_calls = 0;
}
}

extern "C" int rtl8735b_watchdog_start(uint32_t timeout_ms)
{
    start_calls++;
    start_timeout_ms = timeout_ms;
    return start_result;
}

extern "C" void rtl8735b_watchdog_refresh(void)
{
    refresh_calls++;
}

extern "C" uint8_t rtl8735b_reset_was_watchdog(void)
{
    return 0;
}

// BRD_OPTIONS bit0 沒開：不碰硬體，主迴圈的餵狗也不碰硬體。
static void test_disabled_never_touches_hardware()
{
    reset_fake();
    RTL8735B::Watchdog watchdog;
    watchdog.start(false);
    assert(start_calls == 0);
    assert(!watchdog.started());
    for (uint32_t now = 100; now < 110; now++) {
        watchdog.pat(now);
    }
    assert(refresh_calls == 0);
    assert(watchdog.last_pat_ms() == 109);
}

// 開啟：以 2048 ms 啟動一次，之後每次餵狗都刷新硬體。
static void test_enabled_starts_once_and_pat_refreshes()
{
    reset_fake();
    RTL8735B::Watchdog watchdog;
    watchdog.pat(10);
    assert(refresh_calls == 0); // 啟動前的餵狗不碰硬體
    watchdog.start(true);
    assert(start_calls == 1);
    assert(start_timeout_ms == 2048);
    assert(watchdog.started());
    watchdog.pat(20);
    watchdog.pat(21);
    assert(refresh_calls == 2);
    assert(watchdog.last_pat_ms() == 21);
    watchdog.start(true);
    assert(start_calls == 1);
}

// 啟動後致能位元沒有讀回 1：仍視為已下命令並照常餵狗，避免實際在計時的看門狗重置。
static void test_unconfirmed_start_still_refreshes()
{
    reset_fake();
    start_result = -1;
    RTL8735B::Watchdog watchdog;
    watchdog.start(true);
    assert(start_calls == 1);
    assert(watchdog.started());
    watchdog.pat(5);
    assert(refresh_calls == 1);
}

// 預期延遲：時間窗、與進行中的延遲合併取較長者、巢狀取消（比照 ChibiOS Scheduler::expect_delay_ms）。
static void test_expected_delay_window_and_nesting()
{
    RTL8735B::Watchdog watchdog;
    assert(!watchdog.in_expected_delay(1000));
    watchdog.expect_delay(1000, 3000);
    assert(watchdog.in_expected_delay(1000));
    assert(watchdog.in_expected_delay(4000));
    assert(!watchdog.in_expected_delay(4001));

    // 進行中再要求較短的延遲：保留原本剩下的 2000 ms
    watchdog.expect_delay(2000, 500);
    assert(watchdog.in_expected_delay(4000));
    assert(!watchdog.in_expected_delay(4001));
    // 再要求較長的延遲：延長
    watchdog.expect_delay(3000, 5000);
    assert(watchdog.in_expected_delay(8000));
    assert(!watchdog.in_expected_delay(8001));

    // 三層：取消兩層仍在延遲中，取消第三層才結束
    watchdog.expect_delay(3500, 0);
    watchdog.expect_delay(3500, 0);
    assert(watchdog.in_expected_delay(3600));
    watchdog.expect_delay(3600, 0);
    assert(!watchdog.in_expected_delay(3600));
    // 多取消一次不出錯
    watchdog.expect_delay(3700, 0);
    assert(!watchdog.in_expected_delay(3700));

    // millis 回繞時仍以差值判斷
    watchdog.expect_delay(UINT32_MAX - 10, 100);
    assert(watchdog.in_expected_delay(50));
    assert(!watchdog.in_expected_delay(100));
    watchdog.expect_delay(60, 0);
}

int main()
{
    test_disabled_never_touches_hardware();
    test_enabled_starts_once_and_pat_refreshes();
    test_unconfirmed_start_still_refreshes();
    test_expected_delay_window_and_nesting();
    return 0;
}
