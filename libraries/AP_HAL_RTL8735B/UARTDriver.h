/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <AP_HAL/UARTDriver.h>
#include <AP_HAL/utility/RingBuffer.h>
#include <AP_HAL_RTL8735B/Semaphores.h>

#include <FreeRTOS.h>
#include <task.h>

#include <atomic>
#include <stdint.h>

namespace RTL8735B {

class UARTDriver : public AP_HAL::UARTDriver
{
public:
    // 公開 UART API 由 task 呼叫；SDK 中斷只進入 _sdk_irq_handler，不取得 RTOS mutex。
    UARTDriver(uint8_t logical_port, uint8_t uart_id, uint32_t tx_pin, uint32_t rx_pin, bool enabled);
    ~UARTDriver();

    bool is_initialized() override;
    bool tx_pending() override;
    uint32_t txspace() override;
    uint32_t get_baud_rate() const override { return _baudrate.load(); }
    uint32_t bw_in_bytes_per_second() const override { return get_baud_rate() / 10; }
    bool is_owned_by_current_thread() const override;

    bool set_options(uint16_t options) override;
    void configure_parity(uint8_t value) override;
    void set_stop_bits(int count) override;

    void _timer_tick() override;

    uint32_t rx_dropped_bytes() const { return _rx_dropped.load(); }

protected:
    void _begin(uint32_t baud, uint16_t rx_space, uint16_t tx_space) override;
    void _end() override;
    void _flush() override;
    uint32_t _available() override;
    ssize_t _read(uint8_t *buffer, uint16_t count) override;
    size_t _write(const uint8_t *buffer, size_t size) override;
    bool _discard_input() override;

#if HAL_UART_STATS_ENABLED
    uint32_t get_total_tx_bytes() const override { return _tx_bytes.load(); }
    uint32_t get_total_rx_bytes() const override { return _rx_bytes.load(); }
    uint32_t get_total_dropped_rx_bytes() const override { return _rx_dropped.load(); }
#endif

private:
    static constexpr uint8_t UART_INSTANCE_COUNT = 5;
    static constexpr uint32_t DEFAULT_BUFFER_SIZE = 1024;
    static constexpr uint8_t RX_ISR_BUDGET = 32;
    static constexpr uint8_t TX_ISR_BUDGET = 32;

    static void _sdk_irq_handler(uint32_t uart_id, uint32_t irq);
    void _handle_irq(uint32_t irq);
    void _service_tx();
    bool _set_buffers(uint32_t rx_space, uint32_t tx_space);

    const uint8_t _logical_port;
    const uint8_t _uart_id;
    const uint32_t _tx_pin;
    const uint32_t _rx_pin;
    const bool _enabled;
    bool _registered;

    std::atomic<bool> _initialized{false};
    std::atomic<bool> _rx_paused{false};
    std::atomic<bool> _tx_paused{false};
    std::atomic<uint32_t> _baudrate;
    int _stop_bits;
    TaskHandle_t _owner_task;

    ByteBuffer _readbuf{0};
    ByteBuffer _writebuf{0};
    mutable Semaphore _read_mutex;
    Semaphore _write_mutex;

    std::atomic<uint32_t> _rx_dropped{0};
    std::atomic<uint32_t> _rx_bytes{0};
    std::atomic<uint32_t> _tx_bytes{0};

    static UARTDriver *_instances[UART_INSTANCE_COUNT];
};

}
