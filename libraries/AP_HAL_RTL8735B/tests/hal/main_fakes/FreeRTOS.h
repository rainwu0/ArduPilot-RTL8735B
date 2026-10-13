#pragma once
// 測試替身。出處：只宣告受測程式用到的 FreeRTOS API 名稱（FreeRTOS 為 MIT 授權），型別與行為由本測試自訂。
// 韌體入口 main.c 的編譯檢查用：只提供 main.c 用到的型別與巨集
#include <stdint.h>
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t StackType_t;
typedef void *TaskHandle_t;
#define pdPASS 1
#define tskIDLE_PRIORITY 0U
