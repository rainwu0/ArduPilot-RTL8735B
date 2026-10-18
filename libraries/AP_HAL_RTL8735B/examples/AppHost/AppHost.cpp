/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 開發板的應用程式的主程式：給沒有感測器的開發板執行開發板的應用程式（hwdef 的 env RTL8735B_APP_DIR，
 * 見 README〈開發板的應用程式〉）。
 *
 * 只開 Wi-Fi（hwdef 有 HAL_RTL8735B_WIFI_SERIAL 時）與主迴圈，不初始化感測器、不解鎖，PWM 輸出留在安全狀態。
 * 主迴圈每 100 ms 跑一圈，所以 rtl8735b_ardupilot_system_initialized() 為 1、rtl8735b_ardupilot_ms_since_main_loop()
 * 維持在 100 ms 左右，rtl8735b_app_after_setup() 也照常呼叫。一般的載具映像在沒有 IMU 的開發板上會停在
 * 慣性感測器的初始化，setup() 不會結束，主迴圈不會開始。
 *
 *   ./waf configure --board rtl8735b-empty --extra-hwdef <有 env RTL8735B_APP_DIR 的疊加檔>
 *   ./waf --targets examples/AppHost
 */

#include <AP_HAL/AP_HAL.h>

const AP_HAL::HAL& hal = AP_HAL::get_HAL();

void setup()
{
    hal.util->set_soft_armed(false);
    hal.rcout->force_safety_on();
#ifdef HAL_RTL8735B_WIFI_SERIAL
    // Wi-Fi 掛在這個序列埠上，begin() 之後才開始連線
    hal.serial(HAL_RTL8735B_WIFI_SERIAL)->begin(115200);
#endif
}

void loop()
{
    hal.scheduler->delay(100);
}

AP_HAL_MAIN();
