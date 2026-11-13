/* 主機測試用的 FreeRTOS 替身（ota_app_test.c 實作函式）。 */
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
#define pdPASS 1
#define pdFAIL 0
#define pdTRUE 1
#define pdFALSE 0
#define portTICK_PERIOD_MS 1u
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define tskIDLE_PRIORITY 0u
