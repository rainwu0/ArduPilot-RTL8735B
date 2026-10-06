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

#include "Util.h"
#include "RCOutput.h"
#include "wdt_shim.h"

#include <FreeRTOS.h>

using namespace RTL8735B;

AP_HAL::Util::safety_state Util::safety_switch_state()
{
    const AP_HAL::HAL &hal = AP_HAL::get_HAL();
    return static_cast<RTL8735B::RCOutput *>(hal.rcout)->safety_switch_state();
}

/*
  上一次重置是否由 vendor 看門狗造成：開機時讀 AON BOOT_REASON 並清除（wdt_shim.c）。
  這裡回報硬體狀態。本 HAL 沒有跨重置保存的
  persistent_data，回報 true 時 persistent_data 仍是零，共用程式會略過陀螺儀與
  氣壓計校正、DCM 以零姿態起始、不恢復解鎖狀態；HAL_RTL8735B::run() 另記 INTERNAL_ERROR，在重開機前擋住解鎖。
 */
bool Util::was_watchdog_reset() const
{
    return rtl8735b_reset_was_watchdog() != 0;
}

uint32_t Util::available_memory()
{
    // 預設 allocator（heap_4_2.c）的 pvPortMalloc 走 DDR 的 heap0
    return xPortGetFreeHeapSize();
}

void Util::set_hw_rtc(uint64_t time_utc_usec)
{
    _utc_offset_us = time_utc_usec - AP_HAL::micros64();
}

uint64_t Util::get_hw_rtc() const
{
    if (_utc_offset_us == 0) {
        return 0;
    }
    return AP_HAL::micros64() + _utc_offset_us;
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
