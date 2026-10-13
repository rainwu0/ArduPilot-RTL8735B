#pragma once
// 測試替身。出處：只宣告受測程式用到的 FreeRTOS API 名稱（FreeRTOS 為 MIT 授權），型別與行為由本測試自訂。
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint32_t StackType_t;
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef int BaseType_t;
typedef void (*TaskFunction_t)(void *);
#define pdPASS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#ifdef __cplusplus
extern "C" {
#endif
BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t, void *,
                       UBaseType_t, TaskHandle_t *);
void vTaskDelay(TickType_t ticks);
#ifdef __cplusplus
}
#endif
