/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

/*
 * 開發板的應用程式（hwdef 的 env RTL8735B_APP_DIR，見 README〈開發板的應用程式〉）能用的介面。
 * 應用程式只用 SDK 與本標頭，不存取 ArduPilot 的內部。
 *
 * rtl8735b_app_*：由應用程式實作，韌體在下面寫明的時機呼叫。rtl8735b_app_init 有應用程式時必須提供；
 *   其他是選用的，韌體有什麼都不做的弱符號。
 * rtl8735b_ardupilot_*：由 HAL 實作，給應用程式查詢 ArduPilot 的狀態、讓輸出進入安全狀態。
 *   不用 rtl8735b_hal_ 前綴：SDK 的 Wi-Fi 驅動已有 rtl8735b_hal_init 等同前綴的符號。
 *
 * 只有應用程式目錄的原始碼才有的編譯定義（建置時依 hwdef 的 FLASH_LAYOUT）：
 *   RTL8735B_FLASH_LAYOUT_NAME    版面名稱的字串，例如 "realtek-ota"
 *   RTL8735B_FLASH_FW1_ADDR、RTL8735B_FLASH_FW1_LEN、RTL8735B_FLASH_FW2_ADDR、RTL8735B_FLASH_FW2_LEN
 *                                 這份映像打包時分割表裡 fw1、fw2 的位址與長度（bytes）
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 每次開機呼叫一次，在 main() 中、console_init() 與建立 ArduPilot task 之後，vTaskStartScheduler() 之前：
 * - 可以 printf；
 * - 排程器還沒啟動，沒有任何 task 在執行：可以直接以 SDK 讀寫 Flash，必要時可以 sys_reset()；
 * - ArduPilot 的 task 已建立但一行都還沒執行（setup()、Storage、各 HAL task 都還沒跑）。
 *   main() 之前只執行過 C++ 全域物件的建構子，本 HAL 的建構子不讀寫 Flash；
 * - 只能建立 task，不能等待要排程器才會完成的事。
 */
void rtl8735b_app_init(void);

/*
 * 每次開機呼叫一次，在 ArduPilot 的主 task 中、setup() 完成之後、第一次主迴圈之前。
 * 只能建立 task 或設定變數，不能阻塞，否則主迴圈延後開始。
 */
void rtl8735b_app_after_setup(void);

/*
 * 韌體自己要重置之前呼叫：Scheduler::reboot()，例如地面站的重開機指令、要重開才生效的參數。
 * 讓應用程式把會影響下一次開機的硬體放回安全狀態，例如兼作開機模式腳的 GPIO：
 * RTL8735B 在重置時讀 PA5 的電位決定進不進燒錄模式；HUB 8735 ultra 的 D14（PA5）在 variant.h 標為 Sensor power on。
 * 呼叫時輸出已經進入安全狀態，返回後韌體就重置。
 * 可能在任何 task 中呼叫：實作不能阻塞、不能拿鎖、不能印字。
 * 只涵蓋韌體自己的重開機：看門狗、當機都不經過這裡；應用程式自己呼叫 sys_reset() 時，要自己先處理這些硬體。
 */
void rtl8735b_app_before_reset(void);

/*
 * 以下查詢只讀變數，不阻塞、不拿鎖，任何 task 與 FreeRTOS 計時器回呼都可以呼叫。
 * 主迴圈卡住時，前兩個停在最後一次更新的值。
 */

// 1：已解鎖（AP_HAL::Util::get_soft_armed()，馬達測試也算）；0：未解鎖。
int rtl8735b_ardupilot_armed(void);

// 1：setup() 已完成；0：還沒。
int rtl8735b_ardupilot_system_initialized(void);

// 距上一次主迴圈跑完的毫秒數，查詢當下才算，主迴圈卡住時照樣變大；主迴圈還沒跑完過一圈時回傳 UINT32_MAX。
uint32_t rtl8735b_ardupilot_ms_since_main_loop(void);

/*
 * 讓所有 PWM 輸出停止、安全開關回到上鎖（RCOutput::force_safety_on()），例如在應用程式要重置之前。
 * 會等主 task 放開輸出的鎖：可以從任何 FreeRTOS task 呼叫，不能從中斷或 FreeRTOS 計時器回呼呼叫。
 * 重置本身由應用程式以 SDK 的 sys_reset() 進行；重置前要確認未解鎖。
 */
void rtl8735b_ardupilot_outputs_safe(void);

#ifdef __cplusplus
}
#endif
