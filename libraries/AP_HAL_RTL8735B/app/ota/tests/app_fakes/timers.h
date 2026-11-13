/* 主機測試用的 FreeRTOS 軟體計時器替身。 */
#pragma once
#include "FreeRTOS.h"
typedef void *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t timer);
TimerHandle_t xTimerCreate(const char *name, TickType_t period, UBaseType_t auto_reload, void *id,
                           TimerCallbackFunction_t callback);
BaseType_t xTimerStart(TimerHandle_t timer, TickType_t wait);
BaseType_t xTimerStop(TimerHandle_t timer, TickType_t wait);
