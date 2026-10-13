#pragma once
// 測試替身。出處：只宣告受測程式用到的 FreeRTOS API 名稱（FreeRTOS 為 MIT 授權），型別與行為由本測試自訂。
#include <stdint.h>
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#ifdef __cplusplus
extern "C" {
#endif
void vTaskDelay(TickType_t ticks);
#ifdef __cplusplus
}
#endif
