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

class RTL8735B::Util : public AP_HAL::Util
{
public:
    uint32_t available_memory() override;
    safety_state safety_switch_state() override;
    bool was_watchdog_reset() const override;

    // 板上沒有 RTC：以系統計時器加上 set_hw_rtc 給的偏移量模擬，重開機後歸零
    void set_hw_rtc(uint64_t time_utc_usec) override;
    uint64_t get_hw_rtc() const override;

private:
    uint64_t _utc_offset_us;
};
