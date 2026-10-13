#pragma once
// 測試替身。出處：只宣告受測程式用到的 FreeRTOS API 名稱（FreeRTOS 為 MIT 授權），型別與行為由本測試自訂。

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void *);

extern "C" {
BaseType_t xTaskCreate(TaskFunction_t code, const char *name, uint32_t stack_words, void *arg,
                       UBaseType_t priority, TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *previous_wake, TickType_t increment);
TickType_t xTaskGetTickCount(void);
void vTaskPrioritySet(TaskHandle_t task, UBaseType_t priority);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
}
