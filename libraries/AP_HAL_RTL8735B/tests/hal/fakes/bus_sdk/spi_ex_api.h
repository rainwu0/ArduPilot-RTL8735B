#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal_ext/spi_ex_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "objects.h"
#define SPI_STATE_RX_BUSY (1U << 1)
#define SPI_STATE_TX_BUSY (1U << 2)
#ifdef __cplusplus
extern "C" {
#endif
int32_t spi_master_write_read_stream(spi_t *, char *tx, char *rx, uint32_t len);
#ifdef __cplusplus
}
#endif
