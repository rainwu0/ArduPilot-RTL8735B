/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "BusConfig.h"
#include "hwdef.h"
#include <PinNames.h>

// 表格來自開發板的 hwdef（RTL8735B_I2CBUS、RTL8735B_SPIBUS、RTL8735B_SPIDEV，由 rtl8735b_hwdef.py 產生）。
const RTL8735BI2CBusConfig RTL8735B_I2C_BUS_CONFIGS[] = {HAL_RTL8735B_I2C_BUSES};
const uint32_t RTL8735B_I2C_BUS_CONFIG_COUNT =
    sizeof(RTL8735B_I2C_BUS_CONFIGS) / sizeof(RTL8735B_I2C_BUS_CONFIGS[0]);

const RTL8735BSPIBusConfig RTL8735B_SPI_BUS_CONFIGS[] = {HAL_RTL8735B_SPI_BUSES};
const uint32_t RTL8735B_SPI_BUS_CONFIG_COUNT =
    sizeof(RTL8735B_SPI_BUS_CONFIGS) / sizeof(RTL8735B_SPI_BUS_CONFIGS[0]);

const RTL8735BSPIDeviceConfig RTL8735B_SPI_DEVICE_CONFIGS[] = {HAL_RTL8735B_SPI_DEVICES};
const uint32_t RTL8735B_SPI_DEVICE_CONFIG_COUNT =
    sizeof(RTL8735B_SPI_DEVICE_CONFIGS) / sizeof(RTL8735B_SPI_DEVICE_CONFIGS[0]);

// 表的長度要等於產生器寫出的數量（手改 hwdef.h 時才可能不一致）
_Static_assert(sizeof(RTL8735B_I2C_BUS_CONFIGS) / sizeof(RTL8735B_I2C_BUS_CONFIGS[0]) == HAL_RTL8735B_I2C_BUS_COUNT,
               "RTL8735B I2C bus table and count differ");
_Static_assert(sizeof(RTL8735B_SPI_BUS_CONFIGS) / sizeof(RTL8735B_SPI_BUS_CONFIGS[0]) == HAL_RTL8735B_SPI_BUS_COUNT,
               "RTL8735B SPI bus table and count differ");
_Static_assert(sizeof(RTL8735B_SPI_DEVICE_CONFIGS) / sizeof(RTL8735B_SPI_DEVICE_CONFIGS[0]) ==
               HAL_RTL8735B_SPI_DEVICE_COUNT, "RTL8735B SPI device table and count differ");
