#pragma once

// Realtek RTL8735B 的平台預設值。板子專屬的值由 hwdef.dat 產生的 hwdef.h 提供。

#include <hwdef.h>

#define HAL_BOARD_NAME "RTL8735B"
#define HAL_CPU_CLASS HAL_CPU_CLASS_150

// 推論：SDK 預設把一般程式與資料放在 DDR，heap 也在 DDR，
// 所以記憶體等級依 DDR 而不是 512 KB SRAM。連結出映像後以 map 檔確認。
#define HAL_MEM_CLASS HAL_MEM_CLASS_1000

#define HAL_WITH_DRONECAN 0
#define HAL_WITH_UAVCAN 0
#define HAL_MAX_CAN_PROTOCOL_DRIVERS 0
#define HAL_NUM_CAN_IFACES 0
#define HAL_HAVE_SAFETY_SWITCH 0
#define HAL_HAVE_BOARD_VOLTAGE 0
#define HAL_HAVE_SERVO_VOLTAGE 0
#define HAL_WITH_IO_MCU 0

#ifndef HAL_STORAGE_SIZE
#define HAL_STORAGE_SIZE 16384
#endif
#define HAL_STORAGE_SIZE_AVAILABLE HAL_STORAGE_SIZE

// 檔案系統後端（AP_Filesystem_config.h 以這兩個決定 FATFS 與 LittleFS）：SD 卡尚未支援
#ifndef HAL_OS_FATFS_IO
#define HAL_OS_FATFS_IO 0
#endif
#ifndef HAL_OS_LITTLEFS_IO
#define HAL_OS_LITTLEFS_IO 0
#endif

// SDK 分割表的 fw1 區長 0x380000 bytes；映像格式本身另佔空間。
#ifndef HAL_PROGRAM_SIZE_LIMIT_KB
#define HAL_PROGRAM_SIZE_LIMIT_KB 3584
#endif

#ifndef HAL_HAVE_HARDWARE_DOUBLE
#define HAL_HAVE_HARDWARE_DOUBLE 0
#endif

#ifndef HAL_WITH_EKF_DOUBLE
#define HAL_WITH_EKF_DOUBLE HAL_HAVE_HARDWARE_DOUBLE
#endif

#ifdef __cplusplus
// allow for static semaphores
#include <AP_HAL_RTL8735B/Semaphores.h>
#define HAL_Semaphore RTL8735B::Semaphore
#define HAL_BinarySemaphore RTL8735B::BinarySemaphore
#endif

// SDK 的啟動碼以弱符號 main 呼叫應用程式，且在排程器啟動之前。
// ArduPilot 的入口改名，由 AP_HAL_RTL8735B/targets/cmake/main.c 的 main 在 FreeRTOS task 內呼叫。
#define AP_MAIN ardupilot_main
