/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t logical_bus;
    uint32_t sda_pin;
    uint32_t scl_pin;
    uint32_t clock_hz;
} RTL8735BI2CBusConfig;

typedef struct {
    uint8_t logical_bus;
    uint32_t mosi_pin;
    uint32_t miso_pin;
    uint32_t sclk_pin;
    uint32_t default_clock_hz;
} RTL8735BSPIBusConfig;

typedef struct {
    const char *name;
    uint8_t logical_bus;
    uint32_t chip_select_pin;
    uint32_t low_speed_hz;
    uint32_t high_speed_hz;
    uint32_t timeout_ms;
    uint8_t device_id;
    uint8_t mode;
} RTL8735BSPIDeviceConfig;

#ifdef __cplusplus
extern "C" {
#endif

extern const RTL8735BI2CBusConfig RTL8735B_I2C_BUS_CONFIGS[];
extern const uint32_t RTL8735B_I2C_BUS_CONFIG_COUNT;
extern const RTL8735BSPIBusConfig RTL8735B_SPI_BUS_CONFIGS[];
extern const uint32_t RTL8735B_SPI_BUS_CONFIG_COUNT;
extern const RTL8735BSPIDeviceConfig RTL8735B_SPI_DEVICE_CONFIGS[];
extern const uint32_t RTL8735B_SPI_DEVICE_CONFIG_COUNT;

#ifdef __cplusplus
}
#endif
