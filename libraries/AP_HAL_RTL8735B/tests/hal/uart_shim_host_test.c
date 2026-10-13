/*
 * sdk_shim.c 的 UART 中斷順序主機測試。替身 SDK 模擬 IER 的 RX/TX 位元、32 entries 的 RX FIFO
 * （SDK rtl8735b_uart.h 的 Uart_Rx_FIFO_Size）與準位觸發的 RX 中斷；不模擬時序。
 * 替身的 serial_* 一律檢查 serial 物件仍屬本埠，以抓出釋放後還碰 serial 物件的路徑。
 */

#include "sdk_shim.h"
#include "fake_uart_sdk.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { TEST_UART = 2, TEST_TX_PIN = 10, TEST_RX_PIN = 20, RX_FIFO_SIZE = 32 };

static struct {
    int erbi;
    int etbei;
    uint8_t fifo[RX_FIFO_SIZE];
    int count;
    uart_irq_handler rx_callback;
    uint32_t rx_callback_id;
    // 替身 ROM：hal_uart_init 之後 ERBI 的狀態與 FIFO 裡已有的位元組數（實機 ROM 行為不明，取最壞情況）。
    int rom_leaves_erbi;
    int preinit_bytes;
    int erbi_when_hooked;
    int etbei_when_hooked;
    int count_when_hooked;
    int storm;
} uart;

static int received;

static void reset_fake(void)
{
    memset(&uart, 0, sizeof(uart));
    received = 0;
}

static void check(const serial_t *serial)
{
    assert(serial->uart_adp.uart_idx == TEST_UART);
}

static void push_rx(int count)
{
    for (int i = 0; i < count; i++) {
        assert(uart.count < RX_FIFO_SIZE);
        uart.fifo[uart.count++] = (uint8_t)(0x40 + i);
    }
}

uint64_t hal_read_systime_us(void) { return 0; }
void hal_delay_us(uint32_t us) { (void)us; }
void sys_reset(void) {}

uint8_t hal_uart_pin_to_idx(uint8_t pin, uint8_t function)
{
    assert(pin == (function == UART_Pin_TX ? TEST_TX_PIN : TEST_RX_PIN));
    return TEST_UART;
}

hal_status_t hal_uart_init(hal_uart_adapter_t *adapter, uint32_t tx, uint32_t rx, void *config)
{
    assert(tx == TEST_TX_PIN && rx == TEST_RX_PIN && config == NULL);
    adapter->uart_idx = TEST_UART;
    uart.erbi = uart.rom_leaves_erbi;
    uart.etbei = uart.rom_leaves_erbi;
    push_rx(uart.preinit_bytes);
    return HAL_OK;
}

hal_status_t hal_uart_set_baudrate(hal_uart_adapter_t *adapter, uint32_t baud)
{
    assert(adapter->uart_idx == TEST_UART && baud > 0);
    return HAL_OK;
}

hal_status_t hal_uart_set_format(hal_uart_adapter_t *adapter, uint32_t bits, uint32_t parity, uint32_t stop)
{
    assert(adapter->uart_idx == TEST_UART && bits == 8 && parity == ParityNone && stop == 1);
    return HAL_OK;
}

void hal_uart_deinit(hal_uart_adapter_t *adapter)
{
    assert(adapter->uart_idx == TEST_UART);
}

void serial_rx_fifo_level(serial_t *serial, int level)
{
    check(serial);
    assert(level == FifoLv1Byte);
}

void serial_irq_handler(serial_t *serial, uart_irq_handler handler, uint32_t id)
{
    check(serial);
    uart.erbi_when_hooked = uart.erbi;
    uart.etbei_when_hooked = uart.etbei;
    uart.count_when_hooked = uart.count;
    uart.rx_callback = handler;
    uart.rx_callback_id = id;
}

void serial_irq_set(serial_t *serial, SerialIrq irq, uint32_t enable)
{
    check(serial);
    assert(enable <= 1);
    if (irq == RxIrq) {
        uart.erbi = (int)enable;
    } else {
        assert(irq == TxIrq);
        uart.etbei = (int)enable;
    }
}

int serial_readable(serial_t *serial)
{
    check(serial);
    return uart.count > 0;
}

int serial_getc(serial_t *serial)
{
    check(serial);
    assert(uart.count > 0);
    const uint8_t value = uart.fifo[0];
    memmove(uart.fifo, uart.fifo + 1, (size_t)(uart.count - 1));
    uart.count--;
    return value;
}

int serial_writable(serial_t *serial)
{
    check(serial);
    return 1;
}

void serial_putc(serial_t *serial, int byte)
{
    check(serial);
    (void)byte;
}

void serial_clear_rx(serial_t *serial)
{
    check(serial);
    uart.count = 0;
}

uint32_t __get_PRIMASK(void) { return 0; }
void __disable_irq(void) {}
void __set_PRIMASK(uint32_t value) { (void)value; }

// 準位觸發：RX 中斷開著且 FIFO 有資料就持續進 SDK 的 RX callback；callback 返回時既沒讀也沒遮罩即為重入風暴。
static void deliver_rx_level_irq(void)
{
    while (uart.erbi && uart.count > 0) {
        assert(uart.rx_callback != NULL);
        const int before = uart.count;
        uart.rx_callback(uart.rx_callback_id, RxIrq);
        if (uart.erbi && uart.count == before) {
            uart.storm = 1;
            return;
        }
    }
}

static void consumer(uint32_t id, uint32_t event)
{
    assert(id == TEST_UART && event == RTL8735B_UART_IRQ_RX);
    while (rtl8735b_uart_read_byte(TEST_UART) >= 0) {
        received++;
    }
}

// ROM 初始化後若留下 ERBI 與初始化前的位元組，掛 callback 前要先遮罩中斷並讀空 FIFO。
static void test_init_masks_and_drains_before_hooking(void)
{
    reset_fake();
    uart.rom_leaves_erbi = 1;
    uart.preinit_bytes = 5;
    assert(rtl8735b_uart_init(TEST_UART, TEST_TX_PIN, TEST_RX_PIN, 115200) == 0);
    assert(uart.rx_callback != NULL);
    assert(uart.erbi_when_hooked == 0 && uart.etbei_when_hooked == 0);
    assert(uart.count_when_hooked == 0);
    assert(uart.erbi == 0 && uart.etbei == 0);
    rtl8735b_uart_free(TEST_UART);
    puts("PASS init-masks-and-drains-before-hooking");
}

// UARTDriver 掛上 handler 前就來的 RX 中斷：trampoline 要遮罩 RX，不能直接返回；資料留給之後的 handler。
static void test_rx_irq_before_handler_masks_rx(void)
{
    reset_fake();
    assert(rtl8735b_uart_init(TEST_UART, TEST_TX_PIN, TEST_RX_PIN, 115200) == 0);
    push_rx(3);
    uart.erbi = 1;
    deliver_rx_level_irq();
    assert(!uart.storm);
    assert(uart.erbi == 0 && uart.count == 3);

    rtl8735b_uart_set_irq_handler(TEST_UART, consumer);
    rtl8735b_uart_set_irq(TEST_UART, RTL8735B_UART_IRQ_RX, 1);
    deliver_rx_level_irq();
    assert(!uart.storm);
    assert(received == 3 && uart.count == 0 && uart.erbi == 1);
    rtl8735b_uart_free(TEST_UART);
    puts("PASS rx-irq-before-handler-masks-rx");
}

// 釋放後才到的 callback（已鎖存的中斷）不得碰已清零的 serial 物件。
static void test_stale_callback_after_free_touches_nothing(void)
{
    reset_fake();
    assert(rtl8735b_uart_init(TEST_UART, TEST_TX_PIN, TEST_RX_PIN, 115200) == 0);
    const uart_irq_handler callback = uart.rx_callback;
    const uint32_t id = uart.rx_callback_id;
    rtl8735b_uart_free(TEST_UART);
    assert(uart.rx_callback == NULL);
    callback(id, RxIrq);
    callback(id, TxIrq);
    rtl8735b_uart_set_irq_handler(TEST_UART, consumer);
    callback(id, RxIrq);
    assert(received == 0);
    puts("PASS stale-callback-after-free-touches-nothing");
}

int main(void)
{
    test_init_masks_and_drains_before_hooking();
    test_rx_irq_before_handler_masks_rx();
    test_stale_callback_after_free_touches_nothing();
    return 0;
}
