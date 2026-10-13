/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "DeviceBus.h"
#include <AP_HAL/AP_HAL.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace {
struct FakeTask {
    TaskFunction_t entry = nullptr;
    void *argument = nullptr;
    TaskHandle_t handle = nullptr;
    uint32_t stack_words = 0;
    UBaseType_t priority = 0;
    char name[24] = {};
};

FakeTask task;
std::atomic<uint64_t> now_us{0};
std::atomic<bool> stop_task{false};
std::atomic<uint32_t> delay_calls{0};
thread_local TaskHandle_t current_task = reinterpret_cast<TaskHandle_t>(2U);
struct StopTask {};
}

namespace AP_HAL {
uint64_t micros64() { return now_us.load(); }
}

extern "C" BaseType_t xTaskCreate(TaskFunction_t entry, const char *name,
                                    uint32_t stack_words, void *argument,
                                    UBaseType_t priority, TaskHandle_t *handle)
{
    task.entry = entry;
    task.argument = argument;
    task.handle = reinterpret_cast<TaskHandle_t>(1U);
    task.stack_words = stack_words;
    task.priority = priority;
    std::strncpy(task.name, name, sizeof(task.name) - 1U);
    *handle = task.handle;
    return pdPASS;
}

extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return current_task;
}

extern "C" void vTaskDelay(TickType_t ticks)
{
    assert(ticks >= 1U);
    delay_calls++;
    now_us.fetch_add(static_cast<uint64_t>(ticks) * 1000ULL);
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
    if (stop_task.load()) {
        throw StopTask{};
    }
}

static void run_task_until_stopped()
{
    current_task = task.handle;
    try {
        task.entry(task.argument);
    } catch (const StopTask &) {
    }
}

static void test_periodic_callbacks_run_on_bus_task_and_adjust()
{
    now_us = 0U;
    stop_task = false;
    task = FakeTask{};
    RTL8735B::DeviceBus bus("APM_SPI1");
    int hits = 0;
    uint64_t hit_at[2] = {};
    AP_HAL::Device::PeriodicHandle handle = nullptr;
    handle = bus.register_periodic_callback(1000U, [&]() {
        if (hits < 2) {
            hit_at[hits] = now_us.load();
        }
        hits++;
        if (hits == 1) {
            assert(bus.adjust_periodic_callback(handle, 2000U));
        } else {
            assert(bus.unregister_callback(handle));
            stop_task = true;
        }
    });
    assert(handle != nullptr);
    assert(bus.register_periodic_callback(0U, []() {}) == nullptr);
    assert(!bus.adjust_periodic_callback(handle, 2000U));
    assert(task.priority > 4U);
    assert(task.stack_words >= 1024U);
    assert(std::strcmp(task.name, "APM_SPI1") == 0);

    run_task_until_stopped();
    assert(hits == 2);
    assert(now_us.load() >= 3000U);
    // 不早於到期時間：註冊後 1000 us 第一次，調成 2000 us 後再隔 2000 us
    assert(hit_at[0] >= 1000U);
    assert(hit_at[1] >= hit_at[0] + 2000U);
}

static void test_unregister_waits_for_running_callback()
{
    now_us = 0U;
    stop_task = false;
    task = FakeTask{};
    RTL8735B::DeviceBus bus("APM_I2C1");
    std::mutex state_mutex;
    std::condition_variable state_changed;
    bool entered = false;
    bool release_callback = false;
    int callback_count = 0;
    auto handle = bus.register_periodic_callback(1000U, [&]() {
        std::unique_lock<std::mutex> lock(state_mutex);
        entered = true;
        state_changed.notify_all();
        state_changed.wait(lock, [&]() { return release_callback; });
        callback_count++;
    });
    assert(handle != nullptr);

    std::thread worker(run_task_until_stopped);
    {
        std::unique_lock<std::mutex> lock(state_mutex);
        assert(state_changed.wait_for(lock, std::chrono::seconds(2), [&]() { return entered; }));
    }

    std::atomic<bool> unregister_done{false};
    bool unregister_result = false;
    int count_at_unregister = 0;
    std::thread remover([&]() {
        current_task = reinterpret_cast<TaskHandle_t>(3U);
        unregister_result = bus.unregister_callback(handle);
        std::lock_guard<std::mutex> lock(state_mutex);
        count_at_unregister = callback_count;
        unregister_done = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!unregister_done.load());
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        release_callback = true;
    }
    state_changed.notify_all();
    remover.join();
    assert(unregister_result);
    // 放行後到 unregister 取得 bus lock 之前，bus task 可能又跑了幾次（鎖不保證公平）；
    // 契約只要求 unregister 回傳後不再執行。vTaskDelay 再進入兩次，表示其間有一整輪看過 active。
    assert(count_at_unregister >= 1);
    const uint32_t delays_at_unregister = delay_calls.load();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (delay_calls.load() < delays_at_unregister + 2U) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        assert(callback_count == count_at_unregister);
    }
    stop_task = true;
    worker.join();
    assert(!bus.unregister_callback(handle));
}

int main()
{
    test_periodic_callbacks_run_on_bus_task_and_adjust();
    test_unregister_waits_for_running_callback();
}
