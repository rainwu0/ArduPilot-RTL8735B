/*
  Scheduler 的主機測試：編譯正式的 Scheduler.cpp 與 system.cpp，FreeRTOS 與 SDK 系統計時器換成替身時鐘。

  替身時鐘只在 vTaskDelay 與忙等時前進，沒有搶佔、也不計程式本身的執行時間；它驗證的是
  「實際等待不短於要求、超出盡量小」的邏輯，不能代替板上的時序量測。
 */

#include <AP_HAL/AP_HAL.h>

#include "Scheduler.h"
#include "RCInput.h"
#include "sdk_shim.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

using RTL8735B::Scheduler;

namespace {

uint64_t now_us;           // 替身的 hal_read_systime_us()；tick 邊界在 1000 的倍數
uint64_t wake_latency_us;  // vTaskDelay 醒來時比 tick 邊界晚多少（模擬較高優先權的 task 先跑）
uint64_t busy_us;          // rtl8735b_delay_us 忙等的累計
unsigned block_calls;      // vTaskDelay(n > 0) 的次數
unsigned yield_calls;      // vTaskDelay(0) 的次數

int main_task_tag;
int other_task_tag;
int created_task_tag;
TaskHandle_t current_task = &main_task_tag;

AP_HAL::RCOutput fake_rcout;
AP_HAL::Storage fake_storage;
AP_HAL::HAL fake_hal{&fake_rcout, nullptr, &fake_storage, 0};

// 與正式 HAL 一樣是靜態物件（_min_delay_cb_ms 為 0）
Scheduler sched;

unsigned checks;
unsigned failures;

void expect(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void expect(bool ok, const char *fmt, ...)
{
    checks++;
    if (ok) {
        return;
    }
    failures++;
    if (failures <= 25) {
        va_list ap;
        va_start(ap, fmt);
        printf("FAIL: ");
        vprintf(fmt, ap);
        printf("\n");
        va_end(ap);
    }
}

struct Wait {
    int64_t elapsed;
    uint64_t busy;
    unsigned blocks;
    unsigned yields;
};

template <typename F>
Wait measure(TaskHandle_t task, uint64_t start, F f)
{
    current_task = task;
    now_us = start;
    busy_us = 0;
    block_calls = 0;
    yield_calls = 0;
    f();
    return Wait{int64_t(now_us - start), busy_us, block_calls, yield_calls};
}

const char *thread_name(TaskHandle_t task)
{
    return task == &main_task_tag ? "主執行緒" : "其他執行緒";
}

} // namespace

const AP_HAL::HAL &hal = fake_hal;

void AP_HAL::Scheduler::call_delay_cb() {}

RTL8735B::Semaphore::Semaphore() : _handle(nullptr) {}
bool RTL8735B::Semaphore::give() { return true; }
bool RTL8735B::Semaphore::take(uint32_t) { return true; }
bool RTL8735B::Semaphore::take_nonblocking() { return true; }
void RTL8735B::Semaphore::take_blocking() {}
void RTL8735B::RCInput::_timer_tick() {}

extern "C" {

uint64_t rtl8735b_systime_us(void)
{
    return now_us;
}

void rtl8735b_delay_us(uint32_t us)
{
    now_us += us;
    busy_us += us;
}

void rtl8735b_system_reset(void)
{
    abort();
}

// Scheduler.cpp 經正式的 Watchdog.cpp 啟動與餵看門狗、system.cpp 的 panic 會讓 PWM 緊急停止；
// 本測試只驗延遲邏輯，最底層的 SDK 包裝以空替身連結（看門狗預設不啟動）
int rtl8735b_watchdog_start(uint32_t) { return 0; }
void rtl8735b_watchdog_refresh(void) {}
void rtl8735b_pwm_emergency_stop(void) {}

// 照 SDK 的 FreeRTOS（component/os/freertos/freertos_v202012.00/Source/tasks.c）：vTaskDelay(n) 以呼叫當下的
// tick 計數加 n 為醒來時間（prvAddCurrentTaskToDelayedList，5273、5311 行）；tick 中斷把計數加一後
// 到達該值才解除阻塞（xTaskIncrementTick，2743、2762、2785 行）。在一個 tick 之內呼叫，就醒在之後
// 第 n 個 tick 邊界：實際只等 n-1 到 n 個 tick。n 為 0 只重新排程（1343–1372 行）。
void vTaskDelay(TickType_t ticks)
{
    if (ticks == 0) {
        yield_calls++;
        return;
    }
    block_calls++;
    now_us = (now_us / 1000 + ticks) * 1000 + wake_latency_us;
}

void vTaskDelayUntil(TickType_t *, TickType_t) {}

TickType_t xTaskGetTickCount(void)
{
    return TickType_t(now_us / 1000);
}

BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle)
{
    if (handle != nullptr) {
        *handle = &created_task_tag;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t) {}
void vTaskPrioritySet(TaskHandle_t, UBaseType_t) {}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return current_task;
}

} // extern "C"

namespace {

TaskHandle_t const main_task = &main_task_tag;
TaskHandle_t const other_task = &other_task_tag;
TaskHandle_t const both_tasks[] = {main_task, other_task};

// 主執行緒以外的 delay_microseconds：vTaskDelay(ceil(us/1000)) 一次就回來，不檢查經過時間，
// 所以可能短於要求。改成「回來後補 tick、不短於要求」的版本曾出現氣壓計不健康的回歸，原因未明，
// 所以這條路徑先不要求不短於要求。
// TODO(rtl8735b): 查明回歸原因後，改回要求「不短於要求」。
// 這裡只把現況寫成測試（醒在呼叫後第 ceil(us/1000) 個 tick 邊界、不忙等），並統計短於要求的次數，
// 讓行為改變時看得到。
int64_t other_thread_current_wait(uint64_t start, uint16_t us)
{
    if (us == 0) {
        return 0;
    }
    const uint64_t ticks = (uint64_t(us) + 999U) / 1000U;
    return int64_t((start / 1000 + ticks) * 1000 + wake_latency_us - start);
}

unsigned other_short_waits;
unsigned other_waits;

// 交辦的情境：tick 邊界前 0.1 ms 呼叫，vTaskDelay(1) 0.1 ms 後就回來。
void test_call_just_before_tick_edge()
{
    wake_latency_us = 0;
    const uint64_t start = 1000000 - 100;

    static const uint16_t main_us[] = {1000, 1500, 2500};
    for (uint16_t us : main_us) {
        const Wait w = measure(main_task, start, [us] { sched.delay_microseconds(us); });
        expect(w.elapsed >= us, "主執行緒 delay_microseconds(%u) 於 tick 邊界前 100 us 呼叫，只等了 %" PRId64 " us",
               us, w.elapsed);
    }

    // AP_InertialSensor::wait_for_sample 呼叫的是 delay_microseconds_boost（AP_InertialSensor.cpp:2066、2150）
    AP_HAL::Scheduler &base = sched;
    const Wait boost = measure(main_task, start, [&base] { base.delay_microseconds_boost(2500); });
    expect(boost.elapsed >= 2500, "主執行緒 delay_microseconds_boost(2500) 於 tick 邊界前 100 us 呼叫，只等了 %" PRId64 " us",
           boost.elapsed);

    static const uint16_t other_us[] = {250, 1000, 2500};
    for (uint16_t us : other_us) {
        const Wait w = measure(other_task, start, [us] { sched.delay_microseconds(us); });
        // 現況紀錄，不是要求：見 other_thread_current_wait 的說明
        printf("現況：其他執行緒 delay_microseconds(%u) 於 tick 邊界前 100 us 呼叫，等了 %" PRId64 " us\n",
               us, w.elapsed);
        expect(w.elapsed == other_thread_current_wait(start, us),
               "其他執行緒 delay_microseconds(%u) 於 tick 邊界前 100 us 呼叫，等了 %" PRId64 " us，與現況不同",
               us, w.elapsed);
        expect(w.busy == 0, "其他執行緒 delay_microseconds(%u) 忙等了 %" PRIu64 " us", us, w.busy);
    }

    static const uint16_t ms_list[] = {1, 2, 10};
    for (TaskHandle_t task : both_tasks) {
        for (uint16_t ms : ms_list) {
            const Wait w = measure(task, start, [ms] { sched.delay(ms); });
            expect(w.elapsed >= int64_t(ms) * 1000, "%s delay(%u) 於 tick 邊界前 100 us 呼叫，只等了 %" PRId64 " us",
                   thread_name(task), ms, w.elapsed);
        }
    }
}

// 相位 0–999 us、各種長度、兩種執行緒、醒來延遲 0 與 37 us 全掃一遍。
void test_sweep()
{
    static const uint16_t us_list[] = {0, 1, 100, 250, 999, 1000, 1001, 1500, 1999, 2000, 2400, 2500, 2999, 3000, 65535};
    static const uint16_t ms_list[] = {1, 2, 3, 10};
    static const uint64_t latencies[] = {0, 37};

    for (uint64_t latency : latencies) {
        wake_latency_us = latency;
        for (uint64_t phase = 0; phase < 1000; phase++) {
            const uint64_t start = 5000000 + phase;
            for (uint16_t us : us_list) {
                const Wait m = measure(main_task, start, [us] { sched.delay_microseconds(us); });
                expect(m.elapsed >= us, "主執行緒 delay_microseconds(%u) 相位 %" PRIu64 " 延遲 %" PRIu64 "：只等了 %" PRId64 " us",
                       us, phase, latency, m.elapsed);
                expect(m.elapsed - us <= int64_t(latency),
                       "主執行緒 delay_microseconds(%u) 相位 %" PRIu64 " 延遲 %" PRIu64 "：等了 %" PRId64 " us，超出不只醒來延遲",
                       us, phase, latency, m.elapsed);
                expect(m.busy < 1000, "主執行緒 delay_microseconds(%u) 相位 %" PRIu64 "：忙等 %" PRIu64 " us，超過一個 tick",
                       us, phase, m.busy);
                expect(us < 2000 || m.blocks > 0, "主執行緒 delay_microseconds(%u) 相位 %" PRIu64 "：沒有讓出 CPU",
                       us, phase);

                const Wait o = measure(other_task, start, [us] { sched.delay_microseconds(us); });
                // 現況紀錄，不要求不短於要求（見 other_thread_current_wait 的說明）
                other_waits++;
                if (o.elapsed < us) {
                    other_short_waits++;
                }
                expect(o.elapsed == other_thread_current_wait(start, us),
                       "其他執行緒 delay_microseconds(%u) 相位 %" PRIu64 " 延遲 %" PRIu64 "：等了 %" PRId64 " us，與現況不同",
                       us, phase, latency, o.elapsed);
                expect(o.elapsed < int64_t(us) + 1000 + int64_t(latency),
                       "其他執行緒 delay_microseconds(%u) 相位 %" PRIu64 "：等了 %" PRId64 " us，多於一個 tick",
                       us, phase, o.elapsed);
                expect(o.busy == 0, "其他執行緒 delay_microseconds(%u) 相位 %" PRIu64 "：忙等了 %" PRIu64 " us",
                       us, phase, o.busy);
                expect(us != 0 || o.yields == 1, "其他執行緒 delay_microseconds(0) 沒有讓出給同優先權的 task");
            }
            for (uint16_t ms : ms_list) {
                for (TaskHandle_t task : both_tasks) {
                    const Wait w = measure(task, start, [ms] { sched.delay(ms); });
                    expect(w.elapsed >= int64_t(ms) * 1000, "%s delay(%u) 相位 %" PRIu64 "：只等了 %" PRId64 " us",
                           thread_name(task), ms, phase, w.elapsed);
                }
            }
        }
    }
    wake_latency_us = 0;
}

} // namespace

int main()
{
    current_task = main_task;
    sched.init();
    expect(sched.in_main_thread(), "init() 之後呼叫者應是主執行緒");
    current_task = other_task;
    expect(!sched.in_main_thread(), "其他 task 不應被當成主執行緒");

    test_call_just_before_tick_edge();
    test_sweep();

    printf("現況：其他執行緒 delay_microseconds 掃描中短於要求 %u/%u 次（未要求，待追蹤）\n",
           other_short_waits, other_waits);
    printf("scheduler host test: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
