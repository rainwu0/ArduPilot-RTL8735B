/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum rtl8735b_bus_result {
    RTL8735B_BUS_OK = 0,
    RTL8735B_BUS_ERROR = -1,
    RTL8735B_BUS_TIMEOUT = -2,
    RTL8735B_BUS_INVALID = -3,
    RTL8735B_BUS_UNSUPPORTED = -4,
    RTL8735B_BUS_BUSY = -5,
};

int rtl8735b_i2c_init(uint8_t bus, uint32_t sda_pin, uint32_t scl_pin,
                       uint32_t clock_hz);
int rtl8735b_i2c_transfer(uint8_t bus, uint8_t address,
                           const uint8_t *send, uint32_t send_len,
                           uint8_t *recv, uint32_t recv_len,
                           uint8_t split_transfers, uint32_t timeout_ms);

// ss_pin 只用來滿足 SDK hal_ssi_init 的腳位註冊，註冊後即解除；片選仍由 rtl8735b_spi_set_cs 的 GPIO 控制。
int rtl8735b_spi_init(uint8_t bus, uint32_t mosi_pin, uint32_t miso_pin,
                       uint32_t sclk_pin, uint32_t ss_pin, uint32_t clock_hz, uint8_t mode);
int rtl8735b_spi_set_frequency(uint8_t bus, uint32_t clock_hz);
int rtl8735b_spi_set_cs(uint8_t bus, uint32_t cs_pin, uint8_t asserted);
int rtl8735b_spi_transfer(uint8_t bus, uint32_t cs_pin,
                           const uint8_t *send, uint32_t send_len,
                           uint8_t *recv, uint32_t recv_len,
                           uint32_t timeout_ms, uint8_t keep_cs);
int rtl8735b_spi_transfer_fullduplex(uint8_t bus, uint32_t cs_pin,
                                     const uint8_t *send_recv,
                                     uint8_t *recv, uint32_t len,
                                     uint32_t timeout_ms, uint8_t keep_cs);
int rtl8735b_spi_clock_pulse(uint8_t bus, uint32_t len,
                              uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
