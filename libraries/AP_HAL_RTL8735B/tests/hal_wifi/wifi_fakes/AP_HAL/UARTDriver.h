#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define HAL_UART_STATS_ENABLED 1

namespace AP_HAL {

class UARTDriver {
public:
    // 驅動的訊息要到主控台（::printf），不能寫進 Wi-Fi 序列埠本身；每個測試的驅動解構時檢查
    ~UARTDriver() { assert(stream_printf_calls == 0); }

    // 與真的 AP_HAL::BetterStream 相同：子類別的成員函式裡不帶 :: 的 printf 會解析到這個成員，
    // 輸出寫進這個序列埠，而不是主控台。
    virtual void printf(const char *fmt, ...) { (void)fmt; stream_printf_calls++; }
    unsigned stream_printf_calls = 0;

    void begin(uint32_t baud) { _begin(baud, 0, 0); }
    void begin(uint32_t baud, uint16_t rx_space, uint16_t tx_space) { _begin(baud, rx_space, tx_space); }
    size_t write(const uint8_t *buffer, size_t size) { return _write(buffer, size); }
    ssize_t read(uint8_t *buffer, uint16_t count) { return _read(buffer, count); }
    uint32_t available() { return _available(); }
    void end() { _end(); }
    void flush() { _flush(); }
    bool discard_input() { return _discard_input(); }

    virtual uint32_t txspace() = 0;
    virtual bool is_initialized() = 0;
    virtual bool tx_pending() = 0;
    virtual uint32_t get_baud_rate() const { return 0; }
    virtual uint32_t bw_in_bytes_per_second() const { return 0; }
    virtual void _timer_tick() {}
    virtual bool set_options(uint16_t options) { _last_options = options; return options == 0; }
    virtual uint16_t get_options() const { return _last_options; }
    virtual bool is_owned_by_current_thread() const { return true; }
    virtual bool is_network_port() const { return false; }
    virtual void configure_parity(uint8_t value) { parity = value; }
    virtual void set_stop_bits(int count) { (void)count; }

protected:
    virtual void _begin(uint32_t baud, uint16_t rx_space, uint16_t tx_space) = 0;
    virtual size_t _write(const uint8_t *buffer, size_t size) = 0;
    virtual ssize_t _read(uint8_t *buffer, uint16_t count) = 0;
    virtual void _end() = 0;
    virtual void _flush() = 0;
    virtual uint32_t _available() = 0;
    virtual bool _discard_input() = 0;
    virtual uint32_t get_total_tx_bytes() const { return 0; }
    virtual uint32_t get_total_rx_bytes() const { return 0; }
    virtual uint32_t get_total_dropped_rx_bytes() const { return 0; }

    uint32_t lock_write_key = 0;
    uint32_t lock_read_key = 0;
    uint8_t parity = 0;
    uint16_t _last_options = 0;
};

}
