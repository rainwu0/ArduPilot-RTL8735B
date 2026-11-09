/* 主機測試用的 FreeRTOS task 替身。 */
#pragma once
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_words, void *arg, UBaseType_t priority,
                       TaskHandle_t *handle);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
void vTaskDelete(TaskHandle_t task);
/* 主機測試是單執行緒：臨界區不用做事 */
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
