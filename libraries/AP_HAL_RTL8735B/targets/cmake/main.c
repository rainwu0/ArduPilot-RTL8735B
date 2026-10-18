/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * RTL8735B 的韌體入口。
 *
 * SDK 的啟動碼在 __libc_init_array 之後呼叫弱符號 main，
 * 此時 FreeRTOS 排程器還沒啟動；SDK 範例的 main 做完初始化後才呼叫 vTaskStartScheduler
 * （見 SDK project/realtek_amebapro2_v0_example/src/main.c）。
 * 這裡只初始化 console，把 ArduPilot 的入口（AP_HAL_MAIN 展開的 ardupilot_main，
 * 見 AP_HAL/board/rtl8735b.h 的 AP_MAIN）放進一個 task，呼叫開發板的應用程式，再啟動排程器。
 * 應用程式能用的介面集中在 libraries/AP_HAL_RTL8735B/app/rtl8735b_app.h。
 */

#include <stddef.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "rtl8735b_app.h"

extern void console_init(void);
extern int ardupilot_main(int argc, char *const argv[]);

// TODO(rtl8735b): stack 大小與優先權是暫定值；之後由 Scheduler 決定各 task 的配置。
#define ARDUPILOT_TASK_STACK_WORDS ((64 * 1024) / sizeof(StackType_t))
#define ARDUPILOT_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

static void ardupilot_task(void *arg)
{
    (void)arg;
    ardupilot_main(0, NULL);
    vTaskDelete(NULL);
}

// 開發板的應用程式（hwdef 的 env RTL8735B_APP_DIR，見 CMakeLists.txt）定義這個函式，
// 在這裡建立自己的 task；此時排程器還沒啟動。沒有應用程式時是空的弱符號。
// 有應用程式時（CMake 定義 RTL8735B_APP_REQUIRED）不提供弱符號：應用程式漏寫入口就在連結時報錯，
// 不會悄悄變成什麼都不做。
void rtl8735b_app_init(void);
#ifndef RTL8735B_APP_REQUIRED
__attribute__((weak)) void rtl8735b_app_init(void)
{
}
#endif

// 選用的時機（app/rtl8735b_app.h）：有沒有應用程式都是空的弱符號，應用程式要用才定義
// ArduPilot 的主 task 在 setup() 之後、第一次主迴圈之前呼叫（HAL_RTL8735B_Class.cpp）
__attribute__((weak)) void rtl8735b_app_after_setup(void)
{
}

// Scheduler::reboot() 在輸出安全之後、重置之前呼叫
__attribute__((weak)) void rtl8735b_app_before_reset(void)
{
}

int main(void)
{
    console_init();

    if (xTaskCreate(ardupilot_task, "ardupilot", ARDUPILOT_TASK_STACK_WORDS, NULL,
                    ARDUPILOT_TASK_PRIORITY, NULL) != pdPASS) {
        // 沒有飛控 task 不能把只有 SDK task 的系統當成正常開機。
        printf("ArduPilot task allocation failed\n");
        for (;;) {
        }
    }

    rtl8735b_app_init();

    vTaskStartScheduler();
    for (;;) {
    }
}
