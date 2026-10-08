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
 * 改寫自 libraries/AP_HAL_ESP32/system.cpp（ArduPilot 9f648ccabc）。
 */

#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B

#include <AP_Math/div1000.h>

#include <stdarg.h>
#include <stdio.h>

#include "io_shim.h"
#include "sdk_shim.h"

namespace AP_HAL
{

void panic(const char *errormsg, ...)
{
    // 第一步先把四路輸出歸零並鎖住，之後才印訊息（console 輸出可能阻塞）。
    // 本平台沒有 IOMCU、看門狗預設關閉，有刷馬達由
    // MOSFET 直接驅動，輸出停在最後的 duty 會讓馬達一直轉，所以在這裡直接歸零。
    // 不用 RCOutput::force_safety_on()：它以 take_blocking 取 FreeRTOS mutex，在中斷內或關中斷時
    // 不能呼叫，設定的 safety 狀態也會被 force_safety_off() 解除。旗標放在 io_shim，panic 之後
    // 其他執行緒的寫入（例如 timer 執行緒上 Copter failsafe_check 的 motors->output()）只會得到歸零。
    rtl8735b_pwm_emergency_stop();

    va_list ap;

    va_start(ap, errormsg);
    vprintf(errormsg, ap);
    va_end(ap);
    printf("\n");

    // 停在這裡讓 console 看得到訊息。硬體看門狗（BRD_OPTIONS bit0，預設關閉）開啟時，若這裡停住了
    // 主迴圈，逾時後由看門狗重置；panic 發生在優先權低於主迴圈的執行緒時主迴圈照常餵狗，不會重置，
    // 輸出已由上面的緊急停止歸零。
    for (;;) {
    }
}

uint32_t micros()
{
    return micros64();
}

uint32_t millis()
{
    return millis64();
}

uint64_t micros64()
{
    // SDK 系統計時器的 64 位元微秒計數（hal_timer.h:862）；
    // SDK 的 us_ticker 也用同一個來源。推論：系統計時器由 SDK 啟動碼啟動，待板上確認。
    return rtl8735b_systime_us();
}

uint64_t millis64()
{
    return uint64_div1000(micros64());
}

} // namespace AP_HAL

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
