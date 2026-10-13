#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/mbed/hal/spi_api.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。
#include "objects.h"
#ifdef __cplusplus
extern "C" {
#endif
void spi_init(spi_t *, PinName mosi, PinName miso, PinName sclk, PinName ssel);
void spi_free(spi_t *);
void spi_format(spi_t *, int bits, int mode, int slave);
void spi_enable(spi_t *);
void spi_disable(spi_t *);
int spi_busy(spi_t *);
#ifdef __cplusplus
}
#endif
