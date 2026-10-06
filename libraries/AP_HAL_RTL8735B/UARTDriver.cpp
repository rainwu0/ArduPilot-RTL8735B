/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B

#include "UARTDriver.h"

#include "sdk_shim.h"

#include <algorithm>
#include <string.h>

using namespace RTL8735B;

UARTDriver *UARTDriver::_instances[UART_INSTANCE_COUNT] = {};

UARTDriver::UARTDriver(uint8_t logical_port, uint8_t uart_id, uint32_t tx_pin, uint32_t rx_pin, bool enabled) :
    _logical_port(logical_port),
    _uart_id(uart_id),
    _tx_pin(tx_pin),
    _rx_pin(rx_pin),
    _enabled(enabled),
    _registered(false),
    _baudrate(0),
    _stop_bits(1),
    _owner_task(nullptr)
{
    lock_write_key = 0;
    lock_read_key = 0;
    parity = 0;
    _last_options = 0;
    if (_enabled && _uart_id < UART_INSTANCE_COUNT &&
        _uart_id != RTL8735B_UART_LOG_RESERVED && _instances[_uart_id] == nullptr) {
        _instances[_uart_id] = this;
        _registered = true;
    }
}

UARTDriver::~UARTDriver()
{
    _end();
    if (_registered && _uart_id < UART_INSTANCE_COUNT && _instances[_uart_id] == this) {
        _instances[_uart_id] = nullptr;
    }
}

bool UARTDriver::is_initialized()
{
    return _initialized.load();
}

bool UARTDriver::tx_pending()
{
    WITH_SEMAPHORE(_write_mutex);
    return _initialized.load() && _writebuf.available() > 0;
}

uint32_t UARTDriver::txspace()
{
    WITH_SEMAPHORE(_write_mutex);
    return _initialized.load() ? _writebuf.space() : 0;
}

bool UARTDriver::is_owned_by_current_thread() const
{
    WITH_SEMAPHORE(_read_mutex);
    return _initialized.load() && _owner_task == xTaskGetCurrentTaskHandle();
}

bool UARTDriver::set_options(uint16_t options)
{
    if (options != 0) {
        return false;
    }
    _last_options = 0;
    return true;
}

void UARTDriver::configure_parity(uint8_t value)
{
    WITH_SEMAPHORE(_read_mutex);
    WITH_SEMAPHORE(_write_mutex);
    parity = value;
    if (_initialized && parity != 0) {
        _end();
    }
}

void UARTDriver::set_stop_bits(int count)
{
    WITH_SEMAPHORE(_read_mutex);
    WITH_SEMAPHORE(_write_mutex);
    _stop_bits = count;
    if (_initialized && _stop_bits != 1) {
        _end();
    }
}

bool UARTDriver::_set_buffers(uint32_t rx_space, uint32_t tx_space)
{
    const uint32_t rx_capacity = std::max(DEFAULT_BUFFER_SIZE, rx_space);
    const uint32_t tx_capacity = std::max(DEFAULT_BUFFER_SIZE, tx_space);
    if (!_readbuf.set_size(rx_capacity + 1)) {
        return false;
    }
    if (!_writebuf.set_size(tx_capacity + 1)) {
        _readbuf.set_size(0);
        return false;
    }
    return true;
}

void UARTDriver::_begin(uint32_t baud, uint16_t rx_space, uint16_t tx_space)
{
    // 生命週期固定依 read → write 加鎖；兩者都是遞迴 mutex，可在此呼叫 _end()。
    WITH_SEMAPHORE(_read_mutex);
    WITH_SEMAPHORE(_write_mutex);
    if (baud == 0 && rx_space == 0 && tx_space == 0) {
        // begin(0) 不重設 baud 或緩衝，只把讀取所有權交給呼叫 task。
        if (_initialized.load()) {
            _owner_task = xTaskGetCurrentTaskHandle();
        }
        return;
    }

    if (baud == 0 && !_initialized.load()) {
        return;
    }
    const uint32_t requested_baud = baud == 0 ? _baudrate.load() : baud;

    if (!_enabled || !_registered || _uart_id >= UART_INSTANCE_COUNT ||
        _uart_id == RTL8735B_UART_LOG_RESERVED || parity != 0 || _stop_bits != 1) {
        if (_initialized.load()) {
            _end();
        }
        return;
    }

    const uint32_t requested_rx = std::max(DEFAULT_BUFFER_SIZE, (uint32_t)rx_space);
    const uint32_t requested_tx = std::max(DEFAULT_BUFFER_SIZE, (uint32_t)tx_space);
    if (_initialized.load()) {
        const uint32_t current_rx = _readbuf.get_size() > 0 ? _readbuf.get_size() - 1 : 0;
        const uint32_t current_tx = _writebuf.get_size() > 0 ? _writebuf.get_size() - 1 : 0;
        if (requested_rx <= current_rx && requested_tx <= current_tx) {
            // 先遮罩硬體中斷再撤銷 _initialized：撤銷後 ISR 不收資料，夾在兩步之間的 RX 中斷會不斷重入。
            rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_RX, 0);
            rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_TX, 0);
            _initialized.store(false);
            const int status = requested_baud == _baudrate ? 0 : rtl8735b_uart_set_baud(_uart_id, requested_baud);
            if (status == 0) {
                _baudrate = requested_baud;
                _owner_task = xTaskGetCurrentTaskHandle();
                _initialized.store(true);
                rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_TX, 1);
                rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_RX, 1);
                return;
            }
            _end();
            return;
        }
        _end();
    }

    if (!_set_buffers(requested_rx, requested_tx)) {
        _readbuf.set_size(0);
        _writebuf.set_size(0);
        return;
    }

    const int status = rtl8735b_uart_init(_uart_id, _tx_pin, _rx_pin, requested_baud);
    if (status != 0) {
        _readbuf.set_size(0);
        _writebuf.set_size(0);
        return;
    }

    _baudrate = requested_baud;
    _owner_task = xTaskGetCurrentTaskHandle();
    rtl8735b_uart_set_irq_handler(_uart_id, _sdk_irq_handler);
    _initialized.store(true);
    rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_TX, 1);
    rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_RX, 1);
}

void UARTDriver::_end()
{
    _read_mutex.take_blocking();
    _write_mutex.take_blocking();
    // 與 _begin() 相同：先遮罩硬體中斷，再設定讓 ISR 停止服務的旗標。
    if (_registered) {
        rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_RX, 0);
        rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_TX, 0);
    }
    _initialized.store(false);
    _rx_paused.store(true);
    _tx_paused.store(true);
    if (_registered) {
        rtl8735b_uart_set_irq_handler(_uart_id, nullptr);
        rtl8735b_uart_free(_uart_id);
    }
    _baudrate = 0;
    _owner_task = nullptr;
    _readbuf.set_size(0);
    _writebuf.set_size(0);
    _rx_paused.store(false);
    _tx_paused.store(false);
    _write_mutex.give();
    _read_mutex.give();
}

void UARTDriver::_flush()
{
    // flush 讓非阻塞的 TX 緩衝開始送出；接收資料由 discard_input() 專責清除。
    _service_tx();
}

uint32_t UARTDriver::_available()
{
    WITH_SEMAPHORE(_read_mutex);
    if (!_initialized.load() || !is_owned_by_current_thread()) {
        return 0;
    }
    return _readbuf.available();
}

ssize_t UARTDriver::_read(uint8_t *buffer, uint16_t count)
{
    WITH_SEMAPHORE(_read_mutex);
    if (!_initialized.load()) {
        return -1;
    }
    if (!is_owned_by_current_thread()) {
        return -1;
    }
    if (buffer == nullptr || count == 0) {
        return 0;
    }

    const uint32_t read_count = _readbuf.read(buffer, count);
    return read_count;
}

size_t UARTDriver::_write(const uint8_t *buffer, size_t size)
{
    WITH_SEMAPHORE(_write_mutex);
    if (!_initialized.load() || buffer == nullptr || size == 0) {
        return 0;
    }

    const uint32_t written = _writebuf.write(buffer, size);
    if (written > 0) {
        _service_tx();
    }
    return written;
}

bool UARTDriver::_discard_input()
{
    WITH_SEMAPHORE(_read_mutex);
    if (!_initialized.load() || !is_owned_by_current_thread()) {
        return false;
    }

    rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_RX, 0);
    _rx_paused.store(true);
    _readbuf.clear();
    rtl8735b_uart_clear_rx(_uart_id);
    _rx_paused.store(false);
    rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_RX, 1);
    return true;
}

void UARTDriver::_timer_tick()
{
    _service_tx();
}

void UARTDriver::_service_tx()
{
    if (!_initialized.load() || !_write_mutex.take_nonblocking()) {
        return;
    }

    if (_initialized.load() && _writebuf.available() > 0) {
        // 只在取出起送 byte 與送入 FIFO 這段短區間保存 PRIMASK，避免 pending IRQ 搶先取到同一 byte。
        const uint32_t previous_primask = rtl8735b_irq_save_disable();
        _tx_paused.store(true);
        rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_TX, 0);
        const int16_t value = _writebuf.peek(0);
        if (value >= 0 && rtl8735b_uart_writable(_uart_id)) {
            rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_TX, 1);
            if (rtl8735b_uart_write_byte(_uart_id, (uint8_t)value)) {
                _writebuf.advance(1);
                _tx_bytes.fetch_add(1);
            }
        } else {
            // serial_irq_set(TxIrq, 1) 只設軟體旗標；下次 Scheduler tick 再檢查 FIFO 空間。
            rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_TX, 1);
        }
        _tx_paused.store(false);
        rtl8735b_irq_restore(previous_primask);
    }
    _write_mutex.give();
}

void UARTDriver::_sdk_irq_handler(uint32_t uart_id, uint32_t irq)
{
    if (uart_id < UART_INSTANCE_COUNT && _instances[uart_id] != nullptr) {
        _instances[uart_id]->_handle_irq(irq);
    }
}

void UARTDriver::_handle_irq(uint32_t irq)
{
    if (irq == RTL8735B_UART_IRQ_RX) {
        if (!_initialized.load() || _rx_paused.load()) {
            // RX 資料就緒以準位觸發，不讀也不遮罩就返回會立刻重入。task 端已先遮罩再設旗標，
            // 這裡只接已鎖存的中斷；恢復服務的路徑（_begin、_discard_input）都會重新開啟 RX 中斷。
            rtl8735b_uart_set_irq(_uart_id, RTL8735B_UART_IRQ_RX, 0);
            return;
        }
        for (uint8_t i = 0; i < RX_ISR_BUDGET; i++) {
            const int value = rtl8735b_uart_read_byte(_uart_id);
            if (value < 0) {
                break;
            }
            const uint8_t byte = (uint8_t)value;
            _rx_bytes.fetch_add(1);
            if (_readbuf.write(&byte, 1) != 1) {
                _rx_dropped.fetch_add(1);
            }
        }
        return;
    }

    if (irq == RTL8735B_UART_IRQ_TX) {
        // SDK 的 TX 包裝在呼叫前已關閉 ETBEI（serial_api.c 的 _serial_tx_irq_handler），直接返回不會重入。
        if (!_initialized.load() || _tx_paused.load()) {
            return;
        }
        for (uint8_t i = 0; i < TX_ISR_BUDGET; i++) {
            if (!rtl8735b_uart_writable(_uart_id)) {
                break;
            }
            const int16_t byte = _writebuf.peek(0);
            if (byte < 0) {
                break;
            }
            if (!rtl8735b_uart_write_byte(_uart_id, (uint8_t)byte)) {
                break;
            }
            _writebuf.advance(1);
            _tx_bytes.fetch_add(1);
        }
    }
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
