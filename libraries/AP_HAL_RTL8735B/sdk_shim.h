/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

/*
 * SDK 的 hal_*.h 標頭在 C++ 下有型別衝突（fwlib/rtl8735b_snand.h:76 的 typedef snafc_clk_sel_t
 * 與另一處宣告不同），所以需要的少數 SDK HAL 函式由以 C 編譯的 sdk_shim.c 包一層；
 * C++ 端只看這個標頭。mbed 的 PinNames.h 也會經 cmsis.h 引入衝突型別；
 * UART 腳位巨集由 UARTConfig.c 展開，對 C++ 僅提供不含 SDK 型別的 UARTConfig.h。
 */

#include <stdint.h>

enum {
    RTL8735B_UART_COUNT = 5,
    // LOG UART（板上 USB 轉 UART 的那一組）：SDK 開機時就設定好並用來印 printf，shim 沿用它的設定
    RTL8735B_UART_LOG = 1,
    RTL8735B_UART_IRQ_RX = 0,
    RTL8735B_UART_IRQ_TX = 1,
    // hwdef 的 TX 腳寫 PIN_NC（SDK rtl8735b_pin_name.h 的 0x1FF）：只收的埠
    RTL8735B_UART_PIN_NONE = 0x1FF,
};

typedef void (*rtl8735b_uart_irq_handler)(uint32_t uart_id, uint32_t irq);

#ifdef __cplusplus
extern "C" {
#endif

// hal_read_systime_us()：系統計時器的 64 位元微秒計數
uint64_t rtl8735b_systime_us(void);

// hal_delay_us()：忙等
void rtl8735b_delay_us(uint32_t us);

// sys_reset()：系統重置
void rtl8735b_system_reset(void);

// SDK 的 serial_init() 不回傳 hal_uart_init() 錯誤，因此在 C shim 直接保留狀態碼。
int rtl8735b_uart_init(uint8_t uart_id, uint32_t tx_pin, uint32_t rx_pin, uint32_t baud);
int rtl8735b_uart_set_baud(uint8_t uart_id, uint32_t baud);
void rtl8735b_uart_free(uint8_t uart_id);
void rtl8735b_uart_set_irq_handler(uint8_t uart_id, rtl8735b_uart_irq_handler handler);
void rtl8735b_uart_set_irq(uint8_t uart_id, uint32_t irq, uint8_t enable);
int rtl8735b_uart_read_byte(uint8_t uart_id);
uint8_t rtl8735b_uart_writable(uint8_t uart_id);
uint8_t rtl8735b_uart_write_byte(uint8_t uart_id, uint8_t value);
void rtl8735b_uart_clear_rx(uint8_t uart_id);
uint32_t rtl8735b_irq_save_disable(void);
void rtl8735b_irq_restore(uint32_t previous_primask);

#ifdef __cplusplus
}
#endif
