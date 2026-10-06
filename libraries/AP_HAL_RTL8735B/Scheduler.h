/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <AP_HAL/AP_HAL.h>
#include "AP_HAL_RTL8735B_Namespace.h"
#include "Semaphores.h"
#include "Watchdog.h"

#include <FreeRTOS.h>
#include <task.h>

#define RTL8735B_SCHEDULER_MAX_TIMER_PROCS 10
#define RTL8735B_SCHEDULER_MAX_IO_PROCS 10

/*
  Scheduler：主迴圈在 targets/cmake/main.c 建立的 task 內執行（HAL::run 由它呼叫）；
  init() 另外建立 timer（1 kHz）、IO、UART 與 storage 的 FreeRTOS task。
 */
class RTL8735B::Scheduler : public AP_HAL::Scheduler
{
public:
    Scheduler();
    /* AP_HAL::Scheduler methods */
    void     init() override;
    void     delay(uint16_t ms) override;
    void     delay_microseconds(uint16_t us) override;
    void     register_timer_process(AP_HAL::MemberProc) override;
    void     register_io_process(AP_HAL::MemberProc) override;
    void     register_timer_failsafe(AP_HAL::Proc, uint32_t period_us) override;
    void     reboot(bool hold_in_bootloader) override;
    bool     in_main_thread() const override;
    void     set_system_initialized() override;
    bool     is_system_initialized() override;
    void     expect_delay_ms(uint32_t ms) override;
    bool     in_expected_delay(void) const override;

    // 硬體看門狗：HAL_RTL8735B::run() 在 setup() 之後依 BRD_OPTIONS bit0 呼叫 watchdog_start()，
    // 主迴圈每圈 watchdog_pat()（見 Watchdog.h）。
    void     watchdog_start(bool enabled);
    void     watchdog_pat();

    bool thread_create(AP_HAL::MemberProc, const char *name, uint32_t stack_size, priority_base base, int8_t priority) override;

    // FreeRTOS 優先權：SDK 的 configMAX_PRIORITIES 為 11（0–10）；廠商的 Wi-Fi、影像、lwIP、
    // 軟體計時器 task 用 5、6、8、9、10，這裡避開它們。
    static const UBaseType_t TIMER_PRIO   = 7;
    static const UBaseType_t MAIN_PRIO    = 4;
    static const UBaseType_t IO_PRIO      = 3;
    static const UBaseType_t UART_PRIO    = 3;
    static const UBaseType_t RCIN_PRIO    = 3;
    // 本 HAL 的 IO 與 UART
    // 同為 3，storage 至少不低於 IO，否則被壓住超過 2 秒時 healthy() 會誤報 Param storage failed
    static const UBaseType_t STORAGE_PRIO = IO_PRIO;

    // stack 以 word 計（SDK 的 configSTACK_DEPTH_TYPE 是 uint32_t），配置自 DDR 的 heap0
    static const uint32_t TIMER_STACK_WORDS   = 8192 / sizeof(StackType_t);
    static const uint32_t IO_STACK_WORDS      = 8192 / sizeof(StackType_t);
    static const uint32_t UART_STACK_WORDS    = 4096 / sizeof(StackType_t);
    static const uint32_t RCIN_STACK_WORDS    = 4096 / sizeof(StackType_t);
    static const uint32_t STORAGE_STACK_WORDS = 4096 / sizeof(StackType_t);

private:
    static void _timer_thread(void *arg);
    static void _io_thread(void *arg);
    static void _uart_thread(void *arg);
    static void _rcin_thread(void *arg);
    static void _storage_thread(void *arg);
    static void _thread_create_trampoline(void *ctx);

    void _run_timers();
    void _run_io();

    AP_HAL::Proc _failsafe;
    Watchdog _watchdog;

    AP_HAL::MemberProc _timer_proc[RTL8735B_SCHEDULER_MAX_TIMER_PROCS];
    uint8_t _num_timer_procs;
    bool _in_timer_proc;
    Semaphore _timer_sem;

    AP_HAL::MemberProc _io_proc[RTL8735B_SCHEDULER_MAX_IO_PROCS];
    uint8_t _num_io_procs;
    bool _in_io_proc;
    Semaphore _io_sem;

    volatile bool _initialized;

    TaskHandle_t _main_task_handle;
    TaskHandle_t _timer_task_handle;
    TaskHandle_t _io_task_handle;
    TaskHandle_t _uart_task_handle;
    TaskHandle_t _rcin_task_handle;
    TaskHandle_t _storage_task_handle;
};
