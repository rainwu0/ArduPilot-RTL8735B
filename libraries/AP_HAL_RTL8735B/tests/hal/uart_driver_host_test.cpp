#include "UARTDriver.h"
#include "sdk_shim.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <deque>
#include <vector>
#include <chrono>
#include <condition_variable>
#include <thread>

namespace {

constexpr uint8_t kRxIrq = 0;
constexpr uint8_t kTxIrq = 1;
constexpr uint8_t kActiveUart = 2;
constexpr uint32_t kBufferCapacity = 1024;
constexpr uint32_t kIsrBudget = 32;
constexpr uint32_t kRxFifoSize = 32;
constexpr uint32_t kTxFifoSize = 16;

struct FakePort {
    int init_result = 0;
    bool initialized = false;
    bool tx_irq = false;
    bool rx_irq = false;
    bool refuse_write = false;
    uint32_t init_count = 0;
    uint32_t free_count = 0;
    uint32_t baud = 0;
    uint32_t tx_pin = 0;
    uint32_t rx_pin = 0;
    uint32_t clear_rx_count = 0;
    void (*handler)(uint32_t, uint32_t) = nullptr;
    uint32_t handler_id = 0;
    std::deque<uint8_t> rx_fifo;
    std::deque<uint8_t> tx_fifo;
    std::vector<uint8_t> sent;
    // 下一次遮罩 RX 中斷「生效前」抵達的位元組，用來重現旗標與遮罩之間的中斷。
    std::vector<uint8_t> arrive_before_rx_mask;
    bool rx_irq_storm = false;
};

FakePort ports[5];
thread_local int current_task = 1;

FakePort &port(uint8_t id)
{
    assert(id < 5);
    return ports[id];
}

void irq(uint8_t id, uint8_t event)
{
    FakePort &p = port(id);
    assert(p.handler != nullptr);
    p.handler(p.handler_id, event);
}

// RX 資料就緒以準位觸發：中斷開著且 FIFO 有資料就持續進 ISR。ISR 返回時既沒讀走資料也沒遮罩 RX，
// 中斷會立刻重入、task 拿不回 CPU；替身記成 storm 後停止。
void deliver_rx_level_irq(uint8_t id)
{
    FakePort &p = port(id);
    while (p.rx_irq && !p.rx_fifo.empty()) {
        if (p.handler == nullptr) {
            p.rx_irq_storm = true;
            return;
        }
        const size_t before = p.rx_fifo.size();
        p.handler(p.handler_id, kRxIrq);
        if (p.rx_irq && p.rx_fifo.size() == before) {
            p.rx_irq_storm = true;
            return;
        }
    }
}

void drain_tx(uint8_t id)
{
    FakePort &p = port(id);
    while (!p.tx_fifo.empty()) {
        p.sent.push_back(p.tx_fifo.front());
        p.tx_fifo.pop_front();
    }
}

void reset_ports()
{
    for (FakePort &p : ports) {
        p = FakePort{};
    }
}

void test_init_failure_and_disabled_port()
{
    reset_ports();
    ports[kActiveUart].init_result = -7;
    RTL8735B::UARTDriver failed(1, kActiveUart, 41, 42, true);
    failed.begin(115200);
    assert(!failed.is_initialized());
    assert(failed.write(reinterpret_cast<const uint8_t *>("x"), 1) == 0);
    assert(failed.available() == 0);
    assert(!failed.set_options(1));

    RTL8735B::UARTDriver disabled(0, 1, 51, 52, false);
    disabled.begin(115200);
    assert(!disabled.is_initialized());
    assert(ports[1].init_count == 0);
    assert(disabled.txspace() == 0);
    assert(disabled.write(reinterpret_cast<const uint8_t *>("x"), 1) == 0);
}

void test_begin_zero_preserves_configuration_and_transfers_owner()
{
    reset_ports();
    current_task = 1;
    RTL8735B::UARTDriver driver(0, kActiveUart, 41, 42, true);
    driver.begin(115200);
    assert(driver.is_initialized());
    const uint32_t init_count = ports[kActiveUart].init_count;

    current_task = 2;
    driver.begin(0);
    assert(driver.is_initialized());
    assert(driver.get_baud_rate() == 115200);
    assert(ports[kActiveUart].init_count == init_count);
    assert(driver.is_owned_by_current_thread());
    driver.begin(0, 2048, 2048);
    assert(driver.get_baud_rate() == 115200);
    assert(driver.txspace() == 2048);
    assert(ports[kActiveUart].init_count == init_count + 1);
    current_task = 1;
    assert(!driver.is_owned_by_current_thread());
}

void test_rx_isr_budget_order_and_overflow()
{
    reset_ports();
    RTL8735B::UARTDriver driver(0, kActiveUart, 41, 42, true);
    driver.begin(115200, 8, 8);
    constexpr uint16_t total_rx = kBufferCapacity + 12;
    for (uint16_t offset = 0; offset < total_rx; offset += kRxFifoSize) {
        const uint16_t count = std::min<uint16_t>(kRxFifoSize, total_rx - offset);
        for (uint16_t i = 0; i < count; i++) {
            ports[kActiveUart].rx_fifo.push_back(static_cast<uint8_t>(offset + i));
        }
        irq(kActiveUart, kRxIrq);
        assert(ports[kActiveUart].rx_fifo.empty());
    }
    assert(driver.available() == kBufferCapacity);

    uint8_t received[kBufferCapacity]{};
    assert(driver.read(received, sizeof(received)) == kBufferCapacity);
    for (uint32_t i = 0; i < kBufferCapacity; i++) {
        assert(received[i] == static_cast<uint8_t>(i));
    }
    assert(driver.rx_dropped_bytes() == 12);

    ports[kActiveUart].rx_fifo.push_back(0xa1);
    ports[kActiveUart].rx_fifo.push_back(0xa2);
    irq(kActiveUart, kRxIrq);
    uint8_t tail[2]{};
    assert(driver.read(tail, sizeof(tail)) == 2);
    assert(tail[0] == 0xa1 && tail[1] == 0xa2);
}

void test_tx_nonblocking_order_irq_and_scheduler_retry()
{
    reset_ports();
    RTL8735B::UARTDriver driver(0, kActiveUart, 41, 42, true);
    driver.begin(115200, 8, 8);

    ports[kActiveUart].tx_fifo.resize(kTxFifoSize, 0xee);
    uint8_t payload[kBufferCapacity + 6]{};
    for (uint16_t i = 0; i < sizeof(payload); i++) {
        payload[i] = static_cast<uint8_t>(i);
    }
    const size_t accepted = driver.write(payload, sizeof(payload));
    assert(accepted == kBufferCapacity);
    assert(driver.txspace() == 0);
    assert(driver.tx_pending());

    drain_tx(kActiveUart);
    driver._timer_tick();
    assert(ports[kActiveUart].tx_fifo.size() == 1);
    drain_tx(kActiveUart);
    while (driver.tx_pending()) {
        irq(kActiveUart, kTxIrq);
        drain_tx(kActiveUart);
    }
    assert(ports[kActiveUart].sent.size() == accepted + kTxFifoSize);
    for (uint32_t i = 0; i < accepted; i++) {
        assert(ports[kActiveUart].sent[kTxFifoSize + i] == static_cast<uint8_t>(i));
    }
    assert(driver.txspace() == kBufferCapacity);
}

void test_flush_drains_tx_without_discarding_rx()
{
    reset_ports();
    RTL8735B::UARTDriver driver(0, kActiveUart, 41, 42, true);
    driver.begin(115200);
    assert(driver.set_options(0));
    assert(driver.get_options() == 0);
    assert(!driver.set_options(1));
    assert(driver.get_options() == 0);

    ports[kActiveUart].rx_fifo.push_back(0x55);
    irq(kActiveUart, kRxIrq);
    assert(driver.available() == 1);

    ports[kActiveUart].tx_fifo.resize(kTxFifoSize, 0xee);
    const uint8_t outgoing[] = {0x31, 0x32, 0x33};
    assert(driver.write(outgoing, sizeof(outgoing)) == sizeof(outgoing));
    assert(driver.tx_pending());
    drain_tx(kActiveUart);

    driver.flush();
    assert(driver.available() == 1);
    assert(ports[kActiveUart].clear_rx_count == 0);
    assert(ports[kActiveUart].tx_fifo.size() == 1);
    assert(ports[kActiveUart].tx_fifo.front() == outgoing[0]);

    uint8_t incoming = 0;
    assert(driver.read(&incoming, 1) == 1);
    assert(incoming == 0x55);

    ports[kActiveUart].rx_fifo.push_back(0x66);
    irq(kActiveUart, kRxIrq);
    assert(driver.available() == 1);
    assert(driver.discard_input());
    assert(driver.available() == 0);
    assert(ports[kActiveUart].clear_rx_count == 1);
}

void test_end_and_reinitialize()
{
    reset_ports();
    RTL8735B::UARTDriver driver(0, kActiveUart, 41, 42, true);
    driver.begin(115200);

    driver.end();
    assert(!driver.is_initialized());
    assert(ports[kActiveUart].free_count == 1);
    assert(driver.write(reinterpret_cast<const uint8_t *>("x"), 1) == 0);
    driver.begin(57600);
    assert(driver.is_initialized());
    assert(driver.get_baud_rate() == 57600);
    assert(ports[kActiveUart].init_count == 2);
}

// 序列 RC：ArduPilot 在 RC 輸入的埠上輪流試幾種設定（AP_RCProtocol::SerialConfig::apply_to_uart：
// 先設同位與停止位元、開或關 RX 反相，再 begin(baud, 128, 128)）。本 HAL 只支援 8N1、不反相：
// 要求反相時回報不支援、照 8N1 開著；8E2（SBUS）時埠關閉；輪到 8N1 的設定（例如 CRSF 的 416666）時
// 埠重新開啟，收得到資料。接收機用 8N1 不反相的協定（CRSF、ELRS 等）就能接上就用。
void test_serial_rc_probe_cycle()
{
    constexpr uint16_t kOptionRxInv = 1U << 0;  // AP_HAL::UARTDriver::OPTION_RXINV
    reset_ports();
    RTL8735B::UARTDriver driver(4, kActiveUart, 41, 42, true);
    auto apply = [&](uint32_t baud, uint8_t parity, int stop_bits, bool invert) {
        driver.configure_parity(parity);
        driver.set_stop_bits(stop_bits);
        const uint16_t options = invert ? uint16_t(driver.get_options() | kOptionRxInv) :
                                          uint16_t(driver.get_options() & ~kOptionRxInv);
        const bool accepted = driver.set_options(options);
        driver.begin(baud, 128, 128);
        return accepted;
    };

    assert(apply(115200, 0, 1, false));
    assert(driver.is_initialized() && driver.get_baud_rate() == 115200);
    // 反相不支援：回報失敗，埠照 8N1 不反相開著
    assert(!apply(115200, 0, 1, true));
    assert(driver.is_initialized());
    // SBUS（100000、8E2、反相）：埠關閉，不會用錯的格式收資料
    (void)apply(100000, 2, 2, true);
    assert(!driver.is_initialized());
    // CRSF（416666、8N1）：重新開啟
    assert(apply(416666, 0, 1, false));
    assert(driver.is_initialized() && driver.get_baud_rate() == 416666);
    assert(ports[kActiveUart].baud == 416666);

    ports[kActiveUart].rx_fifo = {0xC8, 0x18, 0x16};
    irq(kActiveUart, kRxIrq);
    uint8_t frame[3]{};
    assert(driver.read(frame, sizeof(frame)) == 3);
    assert(frame[0] == 0xC8 && frame[1] == 0x18 && frame[2] == 0x16);

    // CRSFv3 協商的高速率
    assert(apply(2000000, 0, 1, false));
    assert(driver.is_initialized() && ports[kActiveUart].baud == 2000000);
}

// GPS：AP_GPS 自動找鮑率時輪流以各種速率 begin（AP_GPS::_baudrates）；埠開著時只換鮑率、不重新初始化，
// 每一種速率都收得到資料。
void test_gps_autobaud_cycle()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    const uint32_t bauds[] = {9600U, 115200U, 4800U, 19200U, 38400U, 57600U, 230400U, 460800U};
    for (uint32_t baud : bauds) {
        driver.begin(baud);
        assert(driver.is_initialized() && driver.get_baud_rate() == baud);
        assert(ports[kActiveUart].baud == baud);
        ports[kActiveUart].rx_fifo = {0xB5, 0x62};
        irq(kActiveUart, kRxIrq);
        uint8_t sync[2]{};
        assert(driver.read(sync, sizeof(sync)) == 2);
        assert(sync[0] == 0xB5 && sync[1] == 0x62);
    }
    assert(ports[kActiveUart].init_count == 1);
}

void test_write_refusal_preserves_queued_data()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    driver.begin(115200);
    ports[kActiveUart].tx_fifo.resize(kTxFifoSize, 0xee);
    const uint8_t bytes[] = {0x12, 0x34, 0x56};
    assert(driver.write(bytes, sizeof(bytes)) == sizeof(bytes));
    drain_tx(kActiveUart);
    ports[kActiveUart].refuse_write = true;
    driver._timer_tick();
    assert(driver.txspace() == kBufferCapacity - sizeof(bytes));
    irq(kActiveUart, kTxIrq);
    assert(driver.txspace() == kBufferCapacity - sizeof(bytes));
    ports[kActiveUart].refuse_write = false;
    irq(kActiveUart, kTxIrq);
    assert(ports[kActiveUart].tx_fifo.size() == sizeof(bytes));
    for (const uint8_t expected : bytes) {
        assert(ports[kActiveUart].tx_fifo.front() == expected);
        ports[kActiveUart].tx_fifo.pop_front();
    }
}

void test_timer_tick_never_waits_for_writer()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    driver.begin(115200);
    ports[kActiveUart].tx_fifo.resize(kTxFifoSize, 0xee);
    const uint8_t byte = 0xa5;
    assert(driver.write(&byte, 1) == 1);
    drain_tx(kActiveUart);
    const uint32_t before = RTL8735B::Semaphore::blocking_calls;
    RTL8735B::Semaphore::refuse_nonblocking = true;
    driver._timer_tick();
    RTL8735B::Semaphore::refuse_nonblocking = false;
    assert(RTL8735B::Semaphore::blocking_calls == before);
    assert(ports[kActiveUart].tx_fifo.empty());
    driver._timer_tick();
    assert(ports[kActiveUart].tx_fifo.size() == 1);
}

void test_end_between_api_entry_and_lock()
{
    for (bool reading : {false, true}) {
        reset_ports();
        current_task = 1;
        RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
        driver.begin(115200);
        std::mutex gate;
        std::condition_variable changed;
        bool waiting = false, resume = false;
        ssize_t result = -2;
        ByteBuffer::empty_storage_accesses = 0;
        std::thread caller([&] {
            current_task = 1;
            RTL8735B::Semaphore::before_block = [&] {
                std::unique_lock<std::mutex> lock(gate);
                waiting = true;
                changed.notify_all();
                changed.wait(lock, [&] { return resume; });
            };
            uint8_t byte = 0x55;
            result = reading ? driver.read(&byte, 1) : (ssize_t)driver.write(&byte, 1);
        });
        {
            std::unique_lock<std::mutex> lock(gate);
            assert(changed.wait_for(lock, std::chrono::seconds(2), [&] { return waiting; }));
        }
        driver.end();
        {
            std::lock_guard<std::mutex> lock(gate);
            resume = true;
        }
        changed.notify_all();
        caller.join();
        assert(result == (reading ? -1 : 0));
        assert(ByteBuffer::empty_storage_accesses == 0);
    }
}

void test_two_initializers_and_concurrent_queries()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    std::mutex gate;
    std::condition_variable changed;
    unsigned waiting = 0;
    bool resume = false;
    auto begin = [&] {
        RTL8735B::Semaphore::before_block = [&] {
            std::unique_lock<std::mutex> lock(gate);
            waiting++;
            changed.notify_all();
            changed.wait(lock, [&] { return resume; });
        };
        driver.begin(115200);
    };
    std::thread first(begin), second(begin);
    {
        std::unique_lock<std::mutex> lock(gate);
        assert(changed.wait_for(lock, std::chrono::seconds(2), [&] { return waiting == 2; }));
        resume = true;
    }
    changed.notify_all();
    first.join();
    second.join();
    assert(ports[kActiveUart].init_count == 1);
    std::atomic<bool> finished{false};
    std::thread observer([&] {
        current_task = 2;
        while (!finished.load()) {
            assert(driver.available() <= 2048);
            assert(driver.txspace() <= 2048);
            (void)driver.tx_pending();
            const uint32_t baud = driver.get_baud_rate();
            assert(baud == 0 || baud == 115200);
            const uint8_t byte = 0x55;
            assert(driver.write(&byte, 1) <= 1);
        }
    });
    for (unsigned i = 0; i < 100; i++) {
        driver.begin(115200, 2048, 2048);
        driver.end();
    }
    finished.store(true);
    observer.join();
    assert(ByteBuffer::empty_storage_accesses == 0);
}

// 重新 begin（同大小緩衝的快速路徑）時，在遮罩 RX 生效前抵達的位元組要被 ISR 讀走並保留，不得重入。
void test_rebegin_rx_irq_before_mask_is_serviced()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    driver.begin(115200);
    ports[kActiveUart].arrive_before_rx_mask = {0x11, 0x22, 0x33};
    driver.begin(115200);
    assert(!ports[kActiveUart].rx_irq_storm);
    assert(ports[kActiveUart].rx_irq);
    assert(driver.is_initialized());
    uint8_t received[3]{};
    assert(driver.read(received, sizeof(received)) == 3);
    assert(received[0] == 0x11 && received[1] == 0x22 && received[2] == 0x33);
}

// discard_input() 遮罩 RX 生效前抵達的位元組要被讀走（之後一併丟棄），不得重入。
void test_discard_input_rx_irq_before_mask_is_serviced()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    driver.begin(115200);
    ports[kActiveUart].arrive_before_rx_mask = {0x44, 0x55};
    assert(driver.discard_input());
    assert(!ports[kActiveUart].rx_irq_storm);
    assert(driver.available() == 0);
    assert(ports[kActiveUart].rx_irq);
}

// end() 遮罩 RX 生效前抵達的位元組要被讀走，不得重入。
void test_end_rx_irq_before_mask_is_serviced()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    driver.begin(115200);
    ports[kActiveUart].arrive_before_rx_mask = {0x66};
    driver.end();
    assert(!ports[kActiveUart].rx_irq_storm);
    assert(!ports[kActiveUart].rx_irq);
}

// 已鎖存的 RX 中斷在 driver 不收資料時才進 ISR：ISR 要遮罩 RX，不能直接返回。
void test_rx_irq_while_not_accepting_masks_rx()
{
    reset_ports();
    RTL8735B::UARTDriver driver(1, kActiveUart, 41, 42, true);
    driver.begin(115200);
    FakePort &p = ports[kActiveUart];
    const auto handler = p.handler;
    driver.end();
    p.handler = handler;
    p.handler_id = kActiveUart;
    p.rx_irq = true;
    p.rx_fifo = {0x77, 0x88};
    deliver_rx_level_irq(kActiveUart);
    assert(!p.rx_irq_storm);
    assert(!p.rx_irq);
}

}

extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return reinterpret_cast<TaskHandle_t>(static_cast<uintptr_t>(current_task));
}

extern "C" int rtl8735b_uart_init(uint8_t id, uint32_t tx_pin, uint32_t rx_pin, uint32_t baud)
{
    FakePort &p = port(id);
    p.init_count++;
    p.tx_pin = tx_pin;
    p.rx_pin = rx_pin;
    p.baud = baud;
    p.initialized = p.init_result == 0;
    return p.init_result;
}

extern "C" int rtl8735b_uart_set_baud(uint8_t id, uint32_t baud)
{
    port(id).baud = baud;
    return 0;
}

extern "C" void rtl8735b_uart_free(uint8_t id)
{
    FakePort &p = port(id);
    p.free_count++;
    p.initialized = false;
}

extern "C" void rtl8735b_uart_set_irq_handler(uint8_t id, void (*handler)(uint32_t, uint32_t))
{
    port(id).handler = handler;
    port(id).handler_id = id;
}

extern "C" void rtl8735b_uart_set_irq(uint8_t id, uint32_t irq_id, uint8_t enable)
{
    FakePort &p = port(id);
    if (irq_id == kRxIrq) {
        if (!enable && p.rx_irq && !p.arrive_before_rx_mask.empty()) {
            std::vector<uint8_t> bytes;
            bytes.swap(p.arrive_before_rx_mask);
            p.rx_fifo.insert(p.rx_fifo.end(), bytes.begin(), bytes.end());
            deliver_rx_level_irq(id);
        }
        p.rx_irq = enable;
    } else {
        p.tx_irq = enable;
    }
}

extern "C" int rtl8735b_uart_read_byte(uint8_t id)
{
    FakePort &p = port(id);
    if (p.rx_fifo.empty()) {
        return -1;
    }
    const int value = p.rx_fifo.front();
    p.rx_fifo.pop_front();
    return value;
}

extern "C" uint8_t rtl8735b_uart_writable(uint8_t id)
{
    return port(id).tx_fifo.size() < kTxFifoSize;
}

extern "C" uint8_t rtl8735b_uart_write_byte(uint8_t id, uint8_t value)
{
    if (!rtl8735b_uart_writable(id) || port(id).refuse_write) {
        return false;
    }
    port(id).tx_fifo.push_back(value);
    return true;
}

extern "C" void rtl8735b_uart_clear_rx(uint8_t id)
{
    FakePort &p = port(id);
    p.rx_fifo.clear();
    p.clear_rx_count++;
}

extern "C" uint32_t rtl8735b_irq_save_disable(void)
{
    return 0;
}

extern "C" void rtl8735b_irq_restore(uint32_t previous_primask)
{
    (void)previous_primask;
}

int main()
{
    test_init_failure_and_disabled_port();
    test_begin_zero_preserves_configuration_and_transfers_owner();
    test_rx_isr_budget_order_and_overflow();
    test_tx_nonblocking_order_irq_and_scheduler_retry();
    test_flush_drains_tx_without_discarding_rx();
    test_end_and_reinitialize();
    test_timer_tick_never_waits_for_writer();
    test_write_refusal_preserves_queued_data();
    test_end_between_api_entry_and_lock();
    test_two_initializers_and_concurrent_queries();
    test_rebegin_rx_irq_before_mask_is_serviced();
    test_discard_input_rx_irq_before_mask_is_serviced();
    test_end_rx_irq_before_mask_is_serviced();
    test_rx_irq_while_not_accepting_masks_rx();
    test_serial_rc_probe_cycle();
    test_gps_autobaud_cycle();
    puts("UART host tests passed");
    return 0;
}
