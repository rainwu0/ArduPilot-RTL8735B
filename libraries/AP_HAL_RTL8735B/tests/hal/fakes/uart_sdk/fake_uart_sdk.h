#pragma once
// 測試替身。出處：名稱與數值依 SDK d1b6426 的 component/soc/8735b/fwlib/rtl8735b/include/hal_uart.h、component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_uart.h、component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_misc.h、component/soc/8735b/app/stdio_port/stdio_port.h、component/soc/8735b/fwlib/rtl8735b/include/hal_timer.h、component/mbed/hal/serial_api.h、component/mbed/hal_ext/serial_ex_api.h、component/mbed/hal_ext/sys_api.h、component/mbed/targets/hal/rtl8735b/objects.h（Apache-2.0）；只寫受測程式用到的部分，自己撰寫。

/*
 * sdk_shim.c 的 UART 主機測試用替身 SDK：只宣告 shim 用到的型別、常數與函式，
 * 行為由 uart_shim_host_test.c 提供。實際 SDK 型別由 ARM 建置核對。
 */

#include <stdint.h>

typedef int hal_status_t;
// UART 暫存器只留 shim 會碰的中斷致能位元（rtl8735b_uart.h 的 IER：ERBI、ETBEI）
typedef struct {
    struct {
        uint32_t erbi : 1;
        uint32_t etbei : 1;
    } ier_b;
} UART_Type;
typedef struct {
    UART_Type *base_addr;
    uint8_t uart_idx;
    uint8_t tx_pin;
    uint8_t rx_pin;
} hal_uart_adapter_t;
typedef struct { hal_uart_adapter_t uart_adp; } serial_t;
typedef enum { RxIrq, TxIrq } SerialIrq;
typedef void (*uart_irq_handler)(uint32_t id, SerialIrq event);
typedef void (*uart_irq_callback_t)(uint32_t id, uint32_t event);

// 主控台 UART 的 stdio 設定（stdio_port.h）與 ROM 匯出的 misc 函式表（rtl8735b_misc.h）
typedef void (*stdio_putc_t)(void *adapter, const char c);
typedef int (*stdio_getc_t)(void *adapter, char *c);
typedef struct {
    void *adapter;
    stdio_putc_t putc;
    stdio_getc_t getc;
} stdio_port_t;
typedef struct {
    stdio_port_t *pstdio_port;
} hal_misc_func_stubs_t;

enum { HAL_OK = 0, UART_Pin_TX = 0, UART_Pin_RX = 1, ParityNone = 0, FifoLv1Byte = 0 };

uint64_t hal_read_systime_us(void);
void hal_delay_us(uint32_t us);
void sys_reset(void);
uint8_t hal_uart_pin_to_idx(uint8_t pin, uint8_t function);
hal_status_t hal_uart_init(hal_uart_adapter_t *adapter, uint32_t tx, uint32_t rx, void *config);
hal_status_t hal_uart_set_baudrate(hal_uart_adapter_t *adapter, uint32_t baud);
hal_status_t hal_uart_set_format(hal_uart_adapter_t *adapter, uint32_t bits, uint32_t parity, uint32_t stop);
void hal_uart_deinit(hal_uart_adapter_t *adapter);
int hal_uart_readable(hal_uart_adapter_t *adapter);
char hal_uart_getc(hal_uart_adapter_t *adapter);
int hal_uart_writeable(hal_uart_adapter_t *adapter);
void hal_uart_putc(hal_uart_adapter_t *adapter, uint8_t value);
hal_status_t hal_uart_reset_rx_fifo(hal_uart_adapter_t *adapter);
void hal_uart_set_rx_fifo_level(hal_uart_adapter_t *adapter, uint32_t level);
void hal_uart_rxind_hook(hal_uart_adapter_t *adapter, uart_irq_callback_t callback, uint32_t id, uint32_t event);
void hal_uart_txtd_hook(hal_uart_adapter_t *adapter, uart_irq_callback_t callback, uint32_t id, uint32_t event);
void serial_rx_fifo_level(serial_t *serial, int level);
void serial_irq_handler(serial_t *serial, uart_irq_handler handler, uint32_t id);
void serial_irq_set(serial_t *serial, SerialIrq irq, uint32_t enable);
int serial_readable(serial_t *serial);
int serial_writable(serial_t *serial);
int serial_getc(serial_t *serial);
void serial_putc(serial_t *serial, int byte);
void serial_clear_rx(serial_t *serial);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
