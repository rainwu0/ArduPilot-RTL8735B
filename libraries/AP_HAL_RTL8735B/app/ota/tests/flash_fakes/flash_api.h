#pragma once
// SDK component/mbed/hal_ext/flash_api.h 的主機替身。ota_flash_test.c 以 16 MiB 的陣列模擬 NOR Flash，
// 並模擬 XIP：從 fw2 執行時，auto mode 的 flash_stream_read 讀 fw1 範圍會讀到 fw2 的內容。
#include <stdint.h>
typedef struct flash_s flash_t;
void flash_erase_sector(flash_t *obj, uint32_t address);
int flash_read_id(flash_t *obj, uint8_t *buf, uint8_t len);
int flash_stream_read(flash_t *obj, uint32_t address, uint32_t len, uint8_t *data);
int flash_stream_write(flash_t *obj, uint32_t address, uint32_t len, uint8_t *data);
int flash_burst_read(flash_t *obj, uint32_t address, uint32_t Length, uint8_t *data);
