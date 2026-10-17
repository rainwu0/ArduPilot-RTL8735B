#pragma once
// 測試替身。只定義受測程式（AP_Filesystem 的設定與 RTL8735B 後端）用到的板子巨集，自己撰寫。
#define HAL_BOARD_SITL 3
#define HAL_BOARD_LINUX 7
#define HAL_BOARD_CHIBIOS 10
#define HAL_BOARD_ESP32 12
#define HAL_BOARD_QURT 13
#define HAL_BOARD_RTL8735B 100
#define CONFIG_HAL_BOARD HAL_BOARD_RTL8735B
#define HAL_OS_FATFS_IO 0
#define HAL_OS_LITTLEFS_IO 0
#define AP_FILESYSTEM_RTL8735B_ENABLED 1
#define AP_FILESYSTEM_ROMFS_ENABLED 0
#define AP_FILESYSTEM_MISSION_ENABLED 0
#define AP_FILESYSTEM_MAVLOG_ENABLED 0
#define WARN_IF_UNUSED __attribute__((warn_unused_result))
#define CLASS_NO_COPY(c) c(const c &other) = delete; c &operator=(const c&) = delete
