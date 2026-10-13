#pragma once
// 測試替身。出處：只寫 storage_shim.c 用到的名稱（RT_DEV_LOCK_FLASH、device_mutex_lock、device_mutex_unlock）；
// 數值與型別由本測試自訂，主機測試只檢查有沒有上鎖。
#include <stdint.h>
#define RT_DEV_LOCK_FLASH 1U
#ifdef __cplusplus
extern "C" {
#endif
void device_mutex_lock(uint32_t device);
void device_mutex_unlock(uint32_t device);
#ifdef __cplusplus
}
#endif
