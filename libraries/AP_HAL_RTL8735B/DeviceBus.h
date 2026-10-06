/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <AP_HAL/Device.h>
#include <AP_HAL_RTL8735B/Semaphores.h>

#include <FreeRTOS.h>
#include <task.h>

namespace RTL8735B {

class DeviceBus {
public:
    explicit DeviceBus(const char *task_name);

    Semaphore semaphore;

    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb cb);
    bool adjust_periodic_callback(AP_HAL::Device::PeriodicHandle handle,
                                  uint32_t period_usec);
    bool unregister_callback(AP_HAL::Device::PeriodicHandle handle);

private:
    struct Callback {
        Callback *next;
        AP_HAL::Device::PeriodicCb callback;
        uint64_t next_usec;
        uint32_t period_usec;
        uint32_t revision;
        bool active;
    };

    static void _thread(void *arg);
    void _run();

    const char *_task_name;
    Semaphore _callbacks_lock;
    Callback *_callbacks;
    TaskHandle_t _task_handle;
};

}
