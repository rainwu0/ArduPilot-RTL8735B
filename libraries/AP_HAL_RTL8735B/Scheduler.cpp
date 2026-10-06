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

#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B

#include "Scheduler.h"
#include "RCInput.h"

#include <AP_Math/AP_Math.h>
#include <stdio.h>
#include <stdlib.h>

#include <FreeRTOS.h>
#include <task.h>

#include "sdk_shim.h"

extern const AP_HAL::HAL& hal;

using namespace RTL8735B;

Scheduler::Scheduler() :
    _failsafe(nullptr),
    _num_timer_procs(0),
    _in_timer_proc(false),
    _num_io_procs(0),
    _in_io_proc(false),
    _initialized(false),
    _main_task_handle(nullptr),
    _timer_task_handle(nullptr),
    _io_task_handle(nullptr),
    _uart_task_handle(nullptr),
    _rcin_task_handle(nullptr),
    _storage_task_handle(nullptr)
{
}

void Scheduler::init()
{
    // 呼叫者（targets/cmake/main.c 建立的 task）就是主執行緒；把它的優先權調到 MAIN_PRIO
    _main_task_handle = xTaskGetCurrentTaskHandle();
    vTaskPrioritySet(_main_task_handle, MAIN_PRIO);

    if (xTaskCreate(_timer_thread, "APM_TIMER", TIMER_STACK_WORDS, this, TIMER_PRIO, &_timer_task_handle) != pdPASS) {
        AP_HAL::panic("Scheduler: failed to create APM_TIMER");
    }
    if (xTaskCreate(_io_thread, "APM_IO", IO_STACK_WORDS, this, IO_PRIO, &_io_task_handle) != pdPASS) {
        AP_HAL::panic("Scheduler: failed to create APM_IO");
    }
    if (xTaskCreate(_uart_thread, "APM_UART", UART_STACK_WORDS, this, UART_PRIO, &_uart_task_handle) != pdPASS) {
        AP_HAL::panic("Scheduler: failed to create APM_UART");
    }
    if (xTaskCreate(_rcin_thread, "APM_RCIN", RCIN_STACK_WORDS, this, RCIN_PRIO, &_rcin_task_handle) != pdPASS) {
        AP_HAL::panic("Scheduler: failed to create APM_RCIN");
    }
    if (xTaskCreate(_storage_thread, "APM_STORAGE", STORAGE_STACK_WORDS, this, STORAGE_PRIO, &_storage_task_handle) != pdPASS) {
        AP_HAL::panic("Scheduler: failed to create APM_STORAGE");
    }
}

void Scheduler::delay(uint16_t ms)
{
    const uint64_t start = AP_HAL::micros64();
    while ((AP_HAL::micros64() - start) / 1000 < ms) {
        // tick 是 1 ms
        vTaskDelay(1);
        if (_min_delay_cb_ms <= ms && in_main_thread()) {
            call_delay_cb();
        }
    }
}

void Scheduler::delay_microseconds(uint16_t us)
{
    // vTaskDelay(n) 以呼叫當下的 tick 計數加 n 為醒來時間，醒在之後第 n 個 tick 邊界，實際只等
    // n-1 到 n 個 tick（SDK FreeRTOS tasks.c 的 prvAddCurrentTaskToDelayedList 與 xTaskIncrementTick）。
    if (!in_main_thread()) {
        // 主執行緒以外不忙等，無條件進位到 tick 讓出 CPU。
        // AP_Logger 的 log_io 執行緒（PRIORITY_IO+1，與主執行緒同為優先權 4）每圈
        // delay_microseconds(250–1000)；忙等期間不阻塞，優先權 3 以下的 task 拿不到 CPU。
        // 板上實測：log_io 佔 tick 取樣約 45%，
        // APM_WIFI 的 vTaskDelay(1) 最長 686 ms 才回來，UDP 遙測往返延遲到數十秒。
        // tick 是 1 ms；us 為 0 時 vTaskDelay(0) 只讓出給同優先權的 task。
        // TODO(rtl8735b): 這條路徑仍可能短於要求（例如 delay_microseconds(1000) 在 tick 邊界前
        // 呼叫幾乎立刻回來）。
        vTaskDelay(pdMS_TO_TICKS(((uint32_t)us + 999U) / 1000U));
        return;
    }
    // 主執行緒：整數 tick 的部分讓出 CPU，最後不足一個 tick 的部分忙等，維持主迴圈等 IMU 取樣的時序
    // （AP_InertialSensor::wait_for_sample 以 delay_microseconds_boost 等到下一個取樣時刻，本 HAL 不覆寫
    // boost，由 AP_HAL::Scheduler 的預設轉呼叫這裡）。每次只讓 remaining/1000 個 tick：vTaskDelay(n)
    // 最多等 n 個 tick，醒來時不會超過目標；剩下不足一個 tick 的部分忙等補足。超出要求的只有醒來時
    // 被較高優先權 task 延後的時間。
    const uint64_t end = AP_HAL::micros64() + us;
    for (;;) {
        const uint64_t now = AP_HAL::micros64();
        if (now >= end) {
            return;
        }
        const uint64_t remaining = end - now;
        if (remaining >= 1000) {
            vTaskDelay(pdMS_TO_TICKS((uint32_t)(remaining / 1000U)));
        } else {
            rtl8735b_delay_us((uint32_t)remaining);
        }
    }
}

void Scheduler::register_timer_process(AP_HAL::MemberProc proc)
{
    WITH_SEMAPHORE(_timer_sem);
    for (uint8_t i = 0; i < _num_timer_procs; i++) {
        if (_timer_proc[i] == proc) {
            return;
        }
    }
    if (_num_timer_procs >= RTL8735B_SCHEDULER_MAX_TIMER_PROCS) {
        printf("Out of timer processes\n");
        return;
    }
    _timer_proc[_num_timer_procs] = proc;
    _num_timer_procs++;
}

void Scheduler::register_io_process(AP_HAL::MemberProc proc)
{
    WITH_SEMAPHORE(_io_sem);
    for (uint8_t i = 0; i < _num_io_procs; i++) {
        if (_io_proc[i] == proc) {
            return;
        }
    }
    if (_num_io_procs >= RTL8735B_SCHEDULER_MAX_IO_PROCS) {
        printf("Out of IO processes\n");
        return;
    }
    _io_proc[_num_io_procs] = proc;
    _num_io_procs++;
}

void Scheduler::register_timer_failsafe(AP_HAL::Proc failsafe, uint32_t period_us)
{
    _failsafe = failsafe;
}

void Scheduler::reboot(bool hold_in_bootloader)
{
    // TODO(rtl8735b): hold_in_bootloader 沒有對應。燒錄模式由重置時 PA5 的電位決定，
    // 軟體無法選擇。
    hal.rcout->force_safety_on();
    printf("Rebooting\n");
    rtl8735b_system_reset();
    for (;;) {
    }
}

bool Scheduler::in_main_thread() const
{
    return _main_task_handle == xTaskGetCurrentTaskHandle();
}

void Scheduler::set_system_initialized()
{
    if (_initialized) {
        AP_HAL::panic("PANIC: scheduler::system_initialized called more than once");
    }
    _initialized = true;
}

bool Scheduler::is_system_initialized()
{
    return _initialized;
}

/*
  主執行緒告知接下來的操作可能很久（AP_HAL 的 EXPECT_DELAY_MS）。先餵一次狗，
  期間由 timer 執行緒代為餵狗，硬體看門狗才不會在預期中的長延遲（例如加速度計校正）裡重置。
 */
void Scheduler::expect_delay_ms(uint32_t ms)
{
    if (!in_main_thread()) {
        return;
    }
    const uint32_t now = AP_HAL::millis();
    _watchdog.pat(now);
    _watchdog.expect_delay(now, ms);
}

bool Scheduler::in_expected_delay(void) const
{
    // setup() 完成前都算。
    // Flash 抹除後的一段時間不算進來：AP_FlashStorage
    // 只在上鎖時抹除（Storage::_flash_erase_ok），抹除在優先權低於主迴圈的 storage task 進行，也還沒有
    // CPU 會停頓的證據（待量測）。量到停頓再補。
    if (!_initialized) {
        return true;
    }
    return _watchdog.in_expected_delay(AP_HAL::millis());
}

void Scheduler::watchdog_start(bool enabled)
{
    _watchdog.start(enabled);
}

void Scheduler::watchdog_pat()
{
    _watchdog.pat(AP_HAL::millis());
}

void Scheduler::_thread_create_trampoline(void *ctx)
{
    AP_HAL::MemberProc *t = (AP_HAL::MemberProc *)ctx;
    (*t)();
    free(t);
    vTaskDelete(nullptr);
}

bool Scheduler::thread_create(AP_HAL::MemberProc proc, const char *name, uint32_t stack_size, priority_base base, int8_t priority)
{
    // 複製一份 MemberProc，task 結束時釋放
    AP_HAL::MemberProc *tproc = (AP_HAL::MemberProc *)calloc(1, sizeof(proc));
    if (tproc == nullptr) {
        return false;
    }
    *tproc = proc;

    UBaseType_t thread_priority = IO_PRIO;
    static const struct {
        priority_base base;
        UBaseType_t p;
    } priority_map[] = {
        { PRIORITY_BOOST,     TIMER_PRIO },
        { PRIORITY_MAIN,      MAIN_PRIO },
        { PRIORITY_SPI,       TIMER_PRIO },
        { PRIORITY_I2C,       TIMER_PRIO },
        { PRIORITY_CAN,       IO_PRIO },
        { PRIORITY_TIMER,     TIMER_PRIO },
        { PRIORITY_RCOUT,     TIMER_PRIO },
        { PRIORITY_LED,       IO_PRIO },
        { PRIORITY_RCIN,      TIMER_PRIO },
        { PRIORITY_IO,        IO_PRIO },
        { PRIORITY_UART,      UART_PRIO },
        { PRIORITY_STORAGE,   STORAGE_PRIO },
        { PRIORITY_SCRIPTING, IO_PRIO },
        { PRIORITY_NET,       IO_PRIO },
    };
    for (uint8_t i = 0; i < ARRAY_SIZE(priority_map); i++) {
        if (priority_map[i].base == base) {
            thread_priority = constrain_int16(priority_map[i].p + priority, 1, configMAX_PRIORITIES - 1);
            break;
        }
    }

    // stack_size 以 byte 計；多給 1 KB 當作 task 本身的開銷
    const uint32_t stack_words = (stack_size + 1024) / sizeof(StackType_t);
    TaskHandle_t handle;
    if (xTaskCreate(_thread_create_trampoline, name, stack_words, tproc, thread_priority, &handle) != pdPASS) {
        free(tproc);
        return false;
    }
    return true;
}

void Scheduler::_run_timers()
{
    if (_in_timer_proc) {
        return;
    }
    _in_timer_proc = true;

    uint8_t num_procs;
    {
        WITH_SEMAPHORE(_timer_sem);
        num_procs = _num_timer_procs;
    }
    for (uint8_t i = 0; i < num_procs; i++) {
        if (_timer_proc[i]) {
            _timer_proc[i]();
        }
    }
    if (_failsafe != nullptr) {
        _failsafe();
    }

    _in_timer_proc = false;
}

void Scheduler::_timer_thread(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        // 1 kHz（tick 1 ms）
        vTaskDelayUntil(&last_wake, 1);
        sched->_run_timers();
        // 主執行緒宣告的預期延遲期間由 timer 執行緒代為餵狗
        if (sched->in_expected_delay()) {
            sched->watchdog_pat();
        }
    }
}

void Scheduler::_run_io()
{
    if (_in_io_proc) {
        return;
    }
    _in_io_proc = true;

    uint8_t num_procs;
    {
        WITH_SEMAPHORE(_io_sem);
        num_procs = _num_io_procs;
    }
    for (uint8_t i = 0; i < num_procs; i++) {
        if (_io_proc[i]) {
            _io_proc[i]();
        }
    }

    _in_io_proc = false;
}

void Scheduler::_io_thread(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;
    while (!sched->_initialized) {
        vTaskDelay(1);
    }
    while (true) {
        vTaskDelay(1);
        sched->_run_io();
    }
}

void Scheduler::_uart_thread(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        // setup 期間也要重試 FIFO 滿載的傳送，不依賴 IO 回呼或初始化完成。
        for (uint8_t i = 0; i < hal.num_serial; i++) {
            AP_HAL::UARTDriver *uart = hal.serial(i);
            if (uart != nullptr) {
                uart->_timer_tick();
            }
        }
        vTaskDelayUntil(&last_wake, 1);
    }
}

void Scheduler::_rcin_thread(void *arg)
{
    Scheduler *sched = static_cast<Scheduler *>(arg);
    while (!sched->_initialized) {
        vTaskDelay(1);
    }
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        static_cast<RTL8735B::RCInput *>(hal.rcin)->_timer_tick();
        vTaskDelayUntil(&last_wake, 1);
    }
}

void Scheduler::_storage_thread(void *arg)
{
    Scheduler *sched = (Scheduler *)arg;
    while (!sched->_initialized) {
        vTaskDelay(10);
    }
    while (true) {
        vTaskDelay(1);
        hal.storage->_timer_tick();
    }
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
