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

#include <stdint.h>
#include <AP_HAL/AP_HAL_Boards.h>
#include <AP_HAL/AP_HAL_Macros.h>
#include <AP_HAL/Semaphores.h>
#include "AP_HAL_RTL8735B_Namespace.h"

// SDK 的 FreeRTOS（component/os/freertos/freertos_v202012.00）；標頭沒有 freertos/ 前綴
#include <FreeRTOS.h>
#include <semphr.h>

// 遞迴 mutex：AP_HAL::Semaphore 規定同一個 task 可以重複取得
class RTL8735B::Semaphore : public AP_HAL::Semaphore
{
public:
    Semaphore();
    bool give() override;
    bool take(uint32_t timeout_ms) override;
    bool take_nonblocking() override;
    void take_blocking() override;

    bool check_owner();
protected:
    SemaphoreHandle_t _handle;
};

class RTL8735B::BinarySemaphore : public AP_HAL::BinarySemaphore
{
public:
    BinarySemaphore(bool initial_state=false);
    ~BinarySemaphore(void);

    CLASS_NO_COPY(BinarySemaphore);

    bool wait(uint32_t timeout_us) override;
    bool wait_blocking(void) override;
    void signal(void) override;
    void signal_ISR(void) override;

protected:
    SemaphoreHandle_t _sem;
};
