/*
 * 以 C 編譯的 SDK HAL 包裝，理由見 sdk_shim.h。
 */

#include "sdk_shim.h"

#include <hal_timer.h>
#include <sys_api.h>

#include <hal_uart.h>
#include <objects.h>
#include <serial_api.h>
#include <serial_ex_api.h>
#include <cmsis_compiler.h>
#include <string.h>

uint64_t rtl8735b_systime_us(void)
{
    return hal_read_systime_us();
}

void rtl8735b_delay_us(uint32_t us)
{
    hal_delay_us(us);
}

void rtl8735b_system_reset(void)
{
    sys_reset();
}

static serial_t uart_objects[RTL8735B_UART_COUNT];
static uint8_t uart_initialized[RTL8735B_UART_COUNT];
static rtl8735b_uart_irq_handler uart_irq_handlers[RTL8735B_UART_COUNT];

static int _uart_id_valid(uint8_t uart_id)
{
    return uart_id < RTL8735B_UART_COUNT && uart_id != RTL8735B_UART_LOG_RESERVED;
}

// RX FIFO 為 32 entries（SDK fwlib rtl8735b_uart.h:60 的 Uart_Rx_FIFO_Size）；讀空時多留同量給讀取期間新到的位元組。
#define RTL8735B_UART_RX_DRAIN_LIMIT 64U

static void _uart_drain_rx(serial_t *obj)
{
    for (uint32_t i = 0; i < RTL8735B_UART_RX_DRAIN_LIMIT && serial_readable(obj); i++) {
        (void)serial_getc(obj);
    }
}

static void _uart_irq_trampoline(uint32_t uart_id, SerialIrq irq)
{
    // 釋放中或已釋放的埠：serial 物件可能已清零，不碰硬體。
    if (uart_id >= RTL8735B_UART_COUNT || !uart_initialized[uart_id]) {
        return;
    }
    const rtl8735b_uart_irq_handler handler = uart_irq_handlers[uart_id];
    if (handler != NULL) {
        handler(uart_id, (uint32_t)irq);
    } else if (irq == RxIrq) {
        // 還沒有消費者。RX 資料就緒以準位觸發，不讀也不遮罩就返回會立刻重入；遮罩後資料留在 FIFO，
        // 由 UARTDriver 掛上 handler 後重新開啟。TX 不必處理：SDK 的 _serial_tx_irq_handler 呼叫前已關 ETBEI。
        serial_irq_set(&uart_objects[uart_id], RxIrq, 0);
    }
}

int rtl8735b_uart_init(uint8_t uart_id, uint32_t tx_pin, uint32_t rx_pin, uint32_t baud)
{
    if (!_uart_id_valid(uart_id) || baud == 0 || uart_initialized[uart_id] ||
        tx_pin > UINT8_MAX || rx_pin > UINT8_MAX) {
        return -1;
    }

    // UART 選擇由 pinmux 決定；避免錯誤 hwdef 以 UART2 ID 配到 UART1 腳位。
    if (hal_uart_pin_to_idx((uint8_t)tx_pin, UART_Pin_TX) != uart_id ||
        hal_uart_pin_to_idx((uint8_t)rx_pin, UART_Pin_RX) != uart_id) {
        return -1;
    }

    serial_t *obj = &uart_objects[uart_id];
    memset(obj, 0, sizeof(*obj));

    // serial_init() 會忽略此回傳值；直接呼叫 HAL 才能將失敗傳給 UARTDriver。
    hal_status_t status = hal_uart_init(&obj->uart_adp, (uint8_t)tx_pin, (uint8_t)rx_pin, NULL);
    if (status != HAL_OK) {
        return (int)status;
    }
    status = hal_uart_set_baudrate(&obj->uart_adp, baud);
    if (status == HAL_OK) {
        status = hal_uart_set_format(&obj->uart_adp, 8, ParityNone, 1);
    }
    if (status != HAL_OK) {
        hal_uart_deinit(&obj->uart_adp);
        memset(obj, 0, sizeof(*obj));
        return (int)status;
    }

    // ROM 的 hal_uart_init、set_baudrate 是否動到中斷致能不明（ROM 原始碼不在 SDK）。掛 callback 前
    // 一律遮罩 RX/TX 中斷，並讀掉設定鮑率前就收到的位元組；中斷由 UARTDriver 掛上 handler 後才開。
    serial_irq_set(obj, RxIrq, 0);
    serial_irq_set(obj, TxIrq, 0);
    _uart_drain_rx(obj);
    // 最低門檻可在第一個位元組到達時觸發，縮短 MAVLink/GPS 接收延遲。
    serial_rx_fifo_level(obj, FifoLv1Byte);
    // trampoline 只在 uart_initialized 為真時碰 serial 物件，所以先標記再掛 callback。
    uart_initialized[uart_id] = 1;
    serial_irq_handler(obj, _uart_irq_trampoline, uart_id);
    return (int)HAL_OK;
}

int rtl8735b_uart_set_baud(uint8_t uart_id, uint32_t baud)
{
    if (!_uart_id_valid(uart_id) || !uart_initialized[uart_id] || baud == 0) {
        return -1;
    }
    return (int)hal_uart_set_baudrate(&uart_objects[uart_id].uart_adp, baud);
}

void rtl8735b_uart_free(uint8_t uart_id)
{
    if (!_uart_id_valid(uart_id) || !uart_initialized[uart_id]) {
        return;
    }
    serial_irq_set(&uart_objects[uart_id], RxIrq, 0);
    serial_irq_set(&uart_objects[uart_id], TxIrq, 0);
    // 先撤銷標記：之後才進來的已鎖存中斷不再碰即將清零的 serial 物件。
    uart_initialized[uart_id] = 0;
    uart_irq_handlers[uart_id] = NULL;
    serial_irq_handler(&uart_objects[uart_id], NULL, 0);
    hal_uart_deinit(&uart_objects[uart_id].uart_adp);
    memset(&uart_objects[uart_id], 0, sizeof(uart_objects[uart_id]));
}

void rtl8735b_uart_set_irq_handler(uint8_t uart_id, rtl8735b_uart_irq_handler handler)
{
    if (_uart_id_valid(uart_id) && uart_initialized[uart_id]) {
        uart_irq_handlers[uart_id] = handler;
    }
}

void rtl8735b_uart_set_irq(uint8_t uart_id, uint32_t irq, uint8_t enable)
{
    if (!_uart_id_valid(uart_id) || !uart_initialized[uart_id]) {
        return;
    }
    if (irq == RTL8735B_UART_IRQ_RX) {
        serial_irq_set(&uart_objects[uart_id], RxIrq, enable != 0);
    } else if (irq == RTL8735B_UART_IRQ_TX) {
        serial_irq_set(&uart_objects[uart_id], TxIrq, enable != 0);
    }
}

int rtl8735b_uart_read_byte(uint8_t uart_id)
{
    if (!_uart_id_valid(uart_id) || !uart_initialized[uart_id] ||
        !serial_readable(&uart_objects[uart_id])) {
        return -1;
    }
    // serial_getc() 會忙等；單一 RX ISR 消費者先確認 FIFO 非空後才呼叫。
    // hal_uart_getc() 回傳 char 且 SDK 以 -fsigned-char 編譯，serial_getc() 會把 0x80 以上延伸成負數
    // （fwlib/hal_uart.h:255、mbed/serial_api.c:159）；取低 8 位元，負值只代表沒有資料。
    return serial_getc(&uart_objects[uart_id]) & 0xFF;
}

uint8_t rtl8735b_uart_writable(uint8_t uart_id)
{
    return _uart_id_valid(uart_id) && uart_initialized[uart_id] &&
        serial_writable(&uart_objects[uart_id]);
}

uint8_t rtl8735b_uart_write_byte(uint8_t uart_id, uint8_t value)
{
    if (!_uart_id_valid(uart_id) || !uart_initialized[uart_id] ||
        !serial_writable(&uart_objects[uart_id])) {
        return 0;
    }
    // 唯一 TX consumer 已先檢查 FIFO 並遮罩 TX IRQ 起送競爭。
    serial_putc(&uart_objects[uart_id], value);
    return 1;
}

void rtl8735b_uart_clear_rx(uint8_t uart_id)
{
    if (_uart_id_valid(uart_id) && uart_initialized[uart_id]) {
        serial_clear_rx(&uart_objects[uart_id]);
    }
}

uint32_t rtl8735b_irq_save_disable(void)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();
    return previous_primask;
}

void rtl8735b_irq_restore(uint32_t previous_primask)
{
    __set_PRIMASK(previous_primask);
}

/*
 * SDK 的 toolchain.cmake 以 -Wl,-wrap 把 fputc、fputs 導向 __wrap_fputc、__wrap_fputs，
 * 但 SDK 沒有定義它們（SDK 範例沒有呼叫）。libstdc++ 的 std::terminate 處理常式會用到，
 * 這裡轉回 newlib 原本的實作。
 */
#include <stdio.h>
int __real_fputc(int c, FILE *stream);
int __real_fputs(const char *s, FILE *stream);

int __wrap_fputc(int c, FILE *stream)
{
    return __real_fputc(c, stream);
}

int __wrap_fputs(const char *s, FILE *stream)
{
    return __real_fputs(s, stream);
}
