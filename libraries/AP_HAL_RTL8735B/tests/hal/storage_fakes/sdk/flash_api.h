#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal_ext/flash_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
// SDK component/mbed/hal_ext/flash_api.h 的主機替身；函式由 storage_flash_fake.cpp 模擬 NOR 行為。
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct flash_s flash_t;
void flash_erase_sector(flash_t *obj, uint32_t address);
int flash_read_id(flash_t *obj, uint8_t *buf, uint8_t len);
int flash_stream_read(flash_t *obj, uint32_t address, uint32_t len, uint8_t *data);
int flash_stream_write(flash_t *obj, uint32_t address, uint32_t len, uint8_t *data);
#ifdef __cplusplus
}
#endif
