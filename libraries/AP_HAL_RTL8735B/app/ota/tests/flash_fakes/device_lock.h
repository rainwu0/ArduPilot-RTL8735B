#pragma once
// SDK device_lock.h 的主機替身：記錄上鎖次數
#define RT_DEV_LOCK_FLASH 1
void device_mutex_lock(int dev);
void device_mutex_unlock(int dev);
