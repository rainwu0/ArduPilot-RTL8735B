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
 *
 * 改寫自 libraries/AP_HAL_ESP32/Semaphores.cpp（ArduPilot 9f648ccabc）。
 */

#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B

#include "Semaphores.h"

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

using namespace RTL8735B;

// take(timeout) 直接交給 FreeRTOS 等待，
// 不以 delay_microseconds 輪詢；take_nonblocking 取得後保持持有。

Semaphore::Semaphore()
{
    _handle = xSemaphoreCreateRecursiveMutex();
}

bool Semaphore::give()
{
    return xSemaphoreGiveRecursive(_handle) == pdTRUE;
}

bool Semaphore::take(uint32_t timeout_ms)
{
    if (timeout_ms == HAL_SEMAPHORE_BLOCK_FOREVER) {
        take_blocking();
        return true;
    }
    return xSemaphoreTakeRecursive(_handle, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void Semaphore::take_blocking()
{
    xSemaphoreTakeRecursive(_handle, portMAX_DELAY);
}

bool Semaphore::take_nonblocking()
{
    return xSemaphoreTakeRecursive(_handle, 0) == pdTRUE;
}

bool Semaphore::check_owner()
{
    return xSemaphoreGetMutexHolder(_handle) == xTaskGetCurrentTaskHandle();
}

/*
  BinarySemaphore implementation
 */
BinarySemaphore::BinarySemaphore(bool initial_state)
{
    _sem = xSemaphoreCreateBinary();
    if (initial_state) {
        xSemaphoreGive(_sem);
    }
}

bool BinarySemaphore::wait(uint32_t timeout_us)
{
    // tick 是 1 ms；不足一個 tick 的逾時無條件進位，避免變成不等待
    const uint32_t timeout_ms = (timeout_us + 999U) / 1000U;
    return xSemaphoreTake(_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool BinarySemaphore::wait_blocking()
{
    return xSemaphoreTake(_sem, portMAX_DELAY) == pdTRUE;
}

void BinarySemaphore::signal()
{
    xSemaphoreGive(_sem);
}

void BinarySemaphore::signal_ISR()
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(_sem, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

BinarySemaphore::~BinarySemaphore(void)
{
    if (_sem != nullptr) {
        vSemaphoreDelete(_sem);
    }
    _sem = nullptr;
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
