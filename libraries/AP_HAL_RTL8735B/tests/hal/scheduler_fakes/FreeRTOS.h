#pragma once
// 測試替身。出處：只宣告受測程式用到的 FreeRTOS API 名稱（FreeRTOS 為 MIT 授權），型別與行為由本測試自訂；configTICK_RATE_HZ 取 SDK d1b6426 的 project/realtek_amebapro2_v0_example/inc/FreeRTOSConfig.h（MIT）。

#include <stdint.h>

typedef void *TaskHandle_t;
typedef uint32_t StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFAIL 0
// SDK 的 configTICK_RATE_HZ 為 1000（FreeRTOSConfig.h:183），一個 tick 1 ms
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define configMAX_PRIORITIES 11
