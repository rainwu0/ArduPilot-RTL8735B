/*
 * RTL8735B 的韌體入口。
 *
 * SDK 的啟動碼在 __libc_init_array 之後呼叫弱符號 main，
 * 此時 FreeRTOS 排程器還沒啟動；SDK 範例的 main 做完初始化後才呼叫 vTaskStartScheduler
 * （見 SDK project/realtek_amebapro2_v0_example/src/main.c）。
 * 這裡只初始化 console，把 ArduPilot 的入口（AP_HAL_MAIN 展開的 ardupilot_main，
 * 見 AP_HAL/board/rtl8735b.h 的 AP_MAIN）放進一個 task，再啟動排程器。
 */

#include <stddef.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

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

    vTaskStartScheduler();
    for (;;) {
    }
}
