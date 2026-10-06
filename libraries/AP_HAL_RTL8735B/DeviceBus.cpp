/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_HAL/AP_HAL.h>
#include "DeviceBus.h"

#include <new>
#include <task.h>

using namespace RTL8735B;

namespace {
constexpr uint32_t BUS_TASK_STACK_WORDS = 1024;
// 優先權 7 高於 Scheduler::MAIN_PRIO 4，等待樣本時可被喚醒執行。
constexpr UBaseType_t BUS_TASK_PRIORITY = 7;
constexpr uint32_t MAX_IDLE_DELAY_US = 50000;
constexpr uint32_t MIN_IDLE_DELAY_US = 100;
}

DeviceBus::DeviceBus(const char *task_name) :
    _task_name(task_name),
    _callbacks(nullptr),
    _task_handle(nullptr)
{
}

AP_HAL::Device::PeriodicHandle DeviceBus::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    if (period_usec == 0 || !cb) {
        return nullptr;
    }

    semaphore.take_blocking();
    _callbacks_lock.take_blocking();
    Callback *entry = new (std::nothrow) Callback;
    if (entry == nullptr) {
        _callbacks_lock.give();
        semaphore.give();
        return nullptr;
    }
    entry->callback = cb;
    entry->period_usec = period_usec;
    entry->next_usec = AP_HAL::micros64() + period_usec;
    entry->revision = 0;
    entry->active = true;
    entry->next = _callbacks;

    if (_task_handle == nullptr &&
        xTaskCreate(_thread, _task_name, BUS_TASK_STACK_WORDS, this,
                    BUS_TASK_PRIORITY, &_task_handle) != pdPASS) {
        delete entry;
        _task_handle = nullptr;
        _callbacks_lock.give();
        semaphore.give();
        return nullptr;
    }
    _callbacks = entry;
    _callbacks_lock.give();
    semaphore.give();
    return entry;
}

bool DeviceBus::adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle handle, uint32_t period_usec)
{
    if (period_usec == 0 || xTaskGetCurrentTaskHandle() != _task_handle) {
        return false;
    }
    semaphore.take_blocking();
    _callbacks_lock.take_blocking();
    auto *entry = static_cast<Callback *>(handle);
    bool found = false;
    for (Callback *cb = _callbacks; cb != nullptr; cb = cb->next) {
        if (cb == entry && cb->active) {
            cb->period_usec = period_usec;
            cb->next_usec = AP_HAL::micros64() + period_usec;
            cb->revision++;
            found = true;
            break;
        }
    }
    _callbacks_lock.give();
    semaphore.give();
    return found;
}

bool DeviceBus::unregister_callback(AP_HAL::Device::PeriodicHandle handle)
{
    semaphore.take_blocking();
    _callbacks_lock.take_blocking();
    auto *entry = static_cast<Callback *>(handle);
    bool found = false;
    for (Callback *cb = _callbacks; cb != nullptr; cb = cb->next) {
        if (cb == entry && cb->active) {
            cb->active = false;
            found = true;
            break;
        }
    }
    _callbacks_lock.give();
    semaphore.give();
    return found;
}

void DeviceBus::_thread(void *arg)
{
    static_cast<DeviceBus *>(arg)->_run();
}

void DeviceBus::_run()
{
    for (;;) {
        // 先取得 bus lock，讓 callback、register 與 unregister 的鎖順序一致。
        semaphore.take_blocking();
        _callbacks_lock.take_blocking();
        uint64_t now = AP_HAL::micros64();
        for (Callback *cb = _callbacks; cb != nullptr; cb = cb->next) {
            if (!cb->active || now < cb->next_usec) {
                continue;
            }

            const uint64_t scheduled_usec = cb->next_usec;
            const uint32_t revision = cb->revision;
            _callbacks_lock.give();
            cb->callback();
            _callbacks_lock.take_blocking();
            now = AP_HAL::micros64();
            if (cb->active && cb->revision == revision) {
                const uint64_t elapsed = now > scheduled_usec ? now - scheduled_usec : 0U;
                const uint64_t periods = elapsed / cb->period_usec + 1U;
                cb->next_usec = scheduled_usec + periods * cb->period_usec;
            }
        }

        now = AP_HAL::micros64();
        uint32_t delay_usec = MAX_IDLE_DELAY_US;
        for (Callback *cb = _callbacks; cb != nullptr; cb = cb->next) {
            if (cb->active) {
                const uint64_t until_due = cb->next_usec > now ? cb->next_usec - now : 0;
                if (until_due < delay_usec) {
                    delay_usec = static_cast<uint32_t>(until_due);
                }
            }
        }
        _callbacks_lock.give();
        semaphore.give();

        if (delay_usec < MIN_IDLE_DELAY_US) {
            delay_usec = MIN_IDLE_DELAY_US;
        }
        // Always block at least one RTOS tick so this higher-priority task yields
        // CPU to the main loop and lower-priority service tasks between samples.
        const uint32_t delay_ms = (delay_usec + 999U) / 1000U;
        TickType_t ticks = pdMS_TO_TICKS(delay_ms);
        if (ticks == 0U) {
            ticks = 1U;
        }
        vTaskDelay(ticks);
    }
}
