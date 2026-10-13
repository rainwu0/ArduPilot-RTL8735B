#pragma once
// 測試替身。出處：只宣告受測程式用到的 FreeRTOS API 名稱（FreeRTOS 為 MIT 授權），型別與行為由本測試自訂。
#include "FreeRTOS.h"
#ifdef __cplusplus
extern "C" {
#endif
TaskHandle_t xTaskGetCurrentTaskHandle(void);
#ifdef __cplusplus
}
#endif
