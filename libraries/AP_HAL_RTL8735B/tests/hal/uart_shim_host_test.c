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

// LOG UART（SDK 的 UART 1）：SDK 開機時已設定好，由主控台 stdio 持有那個 adapter；
// 替身模擬主控台已開著 RX 中斷、FIFO 裡留著主控台還沒讀走的位元組。
enum { LOG_UART = 1, LOG_TX_PIN = 164, LOG_RX_PIN = 163 };
static UART_Type log_regs;
static hal_uart_adapter_t log_adp;
static stdio_port_t console_port;
const hal_misc_func_stubs_t hal_misc_stubs = { &console_port };

static struct {
    uint8_t fifo[RX_FIFO_SIZE];
    int count;
    uint32_t baud;
    int rx_level;
    int rx_level_set;
    uart_irq_callback_t rx_callback;
    uint32_t rx_callback_id;
    uart_irq_callback_t tx_callback;
    uint32_t tx_callback_id;
    int erbi_when_hooked;
    int etbei_when_hooked;
    int count_when_hooked;
    int sent;
    uint8_t last_tx;
    int storm;
} logu;

static int log_tx_events;

static void reset_fake(void)
{
    memset(&uart, 0, sizeof(uart));
    received = 0;
    memset(&logu, 0, sizeof(logu));
    memset(&log_regs, 0, sizeof(log_regs));
    log_adp = (hal_uart_adapter_t){ &log_regs, LOG_UART, LOG_TX_PIN, LOG_RX_PIN };
    console_port = (stdio_port_t){ &log_adp, NULL, NULL };
    log_regs.ier_b.erbi = 1;
    log_tx_events = 0;
}

static void push_log_rx(int count)
{
    for (int i = 0; i < count; i++) {
        assert(logu.count < RX_FIFO_SIZE);
        logu.fifo[logu.count++] = (uint8_t)(0x60 + i);
    }
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
    // LOG UART 不得重新初始化：SDK 的 printf 仍在用同一組設定
    assert(adapter != &log_adp);
    assert(tx == TEST_TX_PIN && rx == TEST_RX_PIN && config == NULL);
    adapter->uart_idx = TEST_UART;
    uart.erbi = uart.rom_leaves_erbi;
    uart.etbei = uart.rom_leaves_erbi;
    push_rx(uart.preinit_bytes);
    return HAL_OK;
}

hal_status_t hal_uart_set_baudrate(hal_uart_adapter_t *adapter, uint32_t baud)
{
    assert(baud > 0);
    if (adapter == &log_adp) {
        logu.baud = baud;
        return HAL_OK;
    }
    assert(adapter->uart_idx == TEST_UART);
    return HAL_OK;
}

hal_status_t hal_uart_set_format(hal_uart_adapter_t *adapter, uint32_t bits, uint32_t parity, uint32_t stop)
{
    assert(adapter == &log_adp || adapter->uart_idx == TEST_UART);
    assert(bits == 8 && parity == ParityNone && stop == 1);
    return HAL_OK;
}

void hal_uart_deinit(hal_uart_adapter_t *adapter)
{
    assert(adapter != &log_adp);
    assert(adapter->uart_idx == TEST_UART);
}

int hal_uart_readable(hal_uart_adapter_t *adapter)
{
    assert(adapter == &log_adp);
    return logu.count > 0;
}

char hal_uart_getc(hal_uart_adapter_t *adapter)
{
    assert(adapter == &log_adp && logu.count > 0);
    const uint8_t value = logu.fifo[0];
    memmove(logu.fifo, logu.fifo + 1, (size_t)(logu.count - 1));
    logu.count--;
    return (char)value;
}

int hal_uart_writeable(hal_uart_adapter_t *adapter)
{
    assert(adapter == &log_adp);
    return 1;
}

void hal_uart_putc(hal_uart_adapter_t *adapter, uint8_t value)
{
    assert(adapter == &log_adp);
    logu.sent++;
    logu.last_tx = value;
}

hal_status_t hal_uart_reset_rx_fifo(hal_uart_adapter_t *adapter)
{
    assert(adapter == &log_adp);
    logu.count = 0;
    return HAL_OK;
}

void hal_uart_set_rx_fifo_level(hal_uart_adapter_t *adapter, uint32_t level)
{
    assert(adapter == &log_adp);
    logu.rx_level = (int)level;
    logu.rx_level_set = 1;
}

void hal_uart_rxind_hook(hal_uart_adapter_t *adapter, uart_irq_callback_t callback, uint32_t id, uint32_t event)
{
    assert(adapter == &log_adp && event == RxIrq);
    logu.erbi_when_hooked = log_regs.ier_b.erbi;
    logu.etbei_when_hooked = log_regs.ier_b.etbei;
    logu.count_when_hooked = logu.count;
    logu.rx_callback = callback;
    logu.rx_callback_id = id;
}

void hal_uart_txtd_hook(hal_uart_adapter_t *adapter, uart_irq_callback_t callback, uint32_t id, uint32_t event)
{
    assert(adapter == &log_adp && event == TxIrq);
    logu.tx_callback = callback;
    logu.tx_callback_id = id;
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

static void deliver_log_rx_level_irq(void)
{
    while (log_regs.ier_b.erbi && logu.count > 0) {
        assert(logu.rx_callback != NULL);
        const int before = logu.count;
        logu.rx_callback(logu.rx_callback_id, RxIrq);
        if (log_regs.ier_b.erbi && logu.count == before) {
            logu.storm = 1;
            return;
        }
    }
}

static void log_consumer(uint32_t id, uint32_t event)
{
    assert(id == LOG_UART);
    if (event == RTL8735B_UART_IRQ_RX) {
        while (rtl8735b_uart_read_byte(LOG_UART) >= 0) {
            received++;
        }
    } else {
        assert(event == RTL8735B_UART_IRQ_TX);
        log_tx_events++;
    }
}

// LOG UART 沿用 SDK 開機時設好的 adapter，不重新初始化；接手前遮罩中斷、讀掉主控台留下的位元組，
// 再換上自己的 RX、TX callback。
static void test_log_uart_reuses_console_adapter(void)
{
    reset_fake();
    push_log_rx(2);
    assert(rtl8735b_uart_init(LOG_UART, LOG_TX_PIN, LOG_RX_PIN, 57600) == 0);
    assert(logu.baud == 57600);
    assert(logu.rx_level_set && logu.rx_level == FifoLv1Byte);
    assert(logu.rx_callback != NULL && logu.tx_callback != NULL);
    assert(logu.erbi_when_hooked == 0 && logu.etbei_when_hooked == 0);
    assert(logu.count_when_hooked == 0);
    assert(log_regs.ier_b.erbi == 0 && log_regs.ier_b.etbei == 0);

    // RX：與其他埠相同的準位觸發順序
    rtl8735b_uart_set_irq_handler(LOG_UART, log_consumer);
    rtl8735b_uart_set_irq(LOG_UART, RTL8735B_UART_IRQ_RX, 1);
    assert(log_regs.ier_b.erbi == 1);
    push_log_rx(3);
    deliver_log_rx_level_irq();
    assert(!logu.storm && received == 3 && logu.count == 0);

    // TX：開了 TX 中斷時，寫入後打開 ETBEI；TX 中斷先關 ETBEI 再交給 handler
    rtl8735b_uart_set_irq(LOG_UART, RTL8735B_UART_IRQ_TX, 1);
    assert(rtl8735b_uart_writable(LOG_UART));
    assert(rtl8735b_uart_write_byte(LOG_UART, 0x55) == 1);
    assert(logu.sent == 1 && logu.last_tx == 0x55 && log_regs.ier_b.etbei == 1);
    logu.tx_callback(logu.tx_callback_id, TxIrq);
    assert(log_regs.ier_b.etbei == 0 && log_tx_events == 1);
    rtl8735b_uart_set_irq(LOG_UART, RTL8735B_UART_IRQ_TX, 0);
    assert(rtl8735b_uart_write_byte(LOG_UART, 0x56) == 1);
    assert(log_regs.ier_b.etbei == 0);

    push_log_rx(2);
    rtl8735b_uart_clear_rx(LOG_UART);
    assert(logu.count == 0);
    assert(rtl8735b_uart_set_baud(LOG_UART, 115200) == 0 && logu.baud == 115200);

    // 釋放：只遮罩中斷，不 deinit（替身會擋）；之後才到的 callback 不做事
    const uart_irq_callback_t rx_callback = logu.rx_callback;
    rtl8735b_uart_free(LOG_UART);
    assert(log_regs.ier_b.erbi == 0 && log_regs.ier_b.etbei == 0);
    push_log_rx(1);
    rx_callback(LOG_UART, RxIrq);
    assert(received == 3 && logu.count == 1);
    assert(rtl8735b_uart_write_byte(LOG_UART, 0x57) == 0);
    puts("PASS log-uart-reuses-console-adapter");
}

// 拿不到主控台的 adapter，或 hwdef 的腳位和它不同：開啟失敗，不碰硬體。
static void test_log_uart_rejects_unusable_adapter(void)
{
    reset_fake();
    console_port.adapter = NULL;
    assert(rtl8735b_uart_init(LOG_UART, LOG_TX_PIN, LOG_RX_PIN, 115200) == -1);
    assert(logu.rx_callback == NULL && log_regs.ier_b.erbi == 1);

    reset_fake();
    log_adp.tx_pin = 2;
    assert(rtl8735b_uart_init(LOG_UART, LOG_TX_PIN, LOG_RX_PIN, 115200) == -1);
    assert(logu.rx_callback == NULL && logu.baud == 0);
    puts("PASS log-uart-rejects-unusable-adapter");
}

int main(void)
{
    test_log_uart_reuses_console_adapter();
    test_log_uart_rejects_unusable_adapter();
    test_init_masks_and_drains_before_hooking();
    test_rx_irq_before_handler_masks_rx();
    test_stale_callback_after_free_touches_nothing();
    return 0;
}
