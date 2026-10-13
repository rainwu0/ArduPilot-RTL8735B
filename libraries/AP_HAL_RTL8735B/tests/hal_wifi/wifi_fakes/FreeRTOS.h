#pragma once
// 測試替身。出處：只宣告受測程式用到的 FreeRTOS API 名稱（FreeRTOS 為 MIT 授權），型別與行為由本測試自訂。

#include <stdint.h>

typedef void *TaskHandle_t;
typedef uint32_t StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFAIL 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
