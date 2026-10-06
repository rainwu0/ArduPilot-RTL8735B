/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

// SDK 的 PinNames.h 會引入與 C++ 衝突的型別，腳位展開留在 UARTConfig.c。
typedef struct {
    uint8_t uart_id;
    uint32_t tx_pin;
    uint32_t rx_pin;
    uint8_t enabled;
} RTL8735BUARTConfig;

#ifdef __cplusplus
extern "C" {
#endif

extern const RTL8735BUARTConfig RTL8735B_UART_CONFIGS[10];

#ifdef __cplusplus
}
#endif
