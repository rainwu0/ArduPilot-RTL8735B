/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "I2CDevice.h"
#include "BusConfig.h"
#include "bus_shim.h"
#include <AP_HAL/AP_HAL.h>

#include <cassert>
#include <cstdint>
#include <deque>
#include <string>

// 表裡唯一的 I2C 匯流排；run.sh 另以 -DTEST_I2C_BUS=3 -DTEST_I2C_INDEX=2 -DTEST_I2C_INTERNAL=1 再編一次，
// 證明邏輯編號、SDK 編號與內外部都跟著表走。
#ifndef TEST_I2C_BUS
#define TEST_I2C_BUS 1U
#define TEST_I2C_INDEX 1U
#define TEST_I2C_INTERNAL 0U
#endif

namespace {
uint64_t now_us;
uint32_t init_calls;
uint8_t fail_init_bus = 0xFFU;
uint8_t last_bus;
const char *created_tasks[4];
uint32_t created_task_count;
uint8_t initialized_bus;
uint8_t initialized_sdk_index;
uint32_t initialized_sda;
uint32_t initialized_scl;
uint32_t initialized_clock;
uint32_t transfer_calls;
uint32_t started_transactions;
uint8_t last_address;
uint32_t last_timeout_ms;
bool poisoned;
std::deque<int> responses;
thread_local TaskHandle_t current_task = reinterpret_cast<TaskHandle_t>(2U);
}

extern "C" BaseType_t xTaskCreate(TaskFunction_t, const char *name, uint32_t,
                                    void *, UBaseType_t, TaskHandle_t *)
{
    if (created_task_count < 4U) {
        created_tasks[created_task_count++] = name;
    }
    return pdPASS;
}
extern "C" void vTaskDelay(TickType_t ticks)
{
    now_us += static_cast<uint64_t>(ticks) * 1000ULL;
}
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }

namespace AP_HAL {
uint64_t micros64() { return now_us; }
}

extern "C" int rtl8735b_i2c_init(uint8_t bus, uint8_t sdk_index, uint32_t sda, uint32_t scl,
                                   uint32_t clock_hz)
{
    init_calls++;
    initialized_bus = bus;
    initialized_sdk_index = sdk_index;
    initialized_sda = sda;
    initialized_scl = scl;
    initialized_clock = clock_hz;
    return bus == fail_init_bus ? RTL8735B_BUS_ERROR : RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_i2c_transfer(uint8_t bus, uint8_t address,
                                       const uint8_t *, uint32_t,
                                       uint8_t *, uint32_t,
                                       uint8_t, uint32_t timeout_ms)
{
    transfer_calls++;
    last_bus = bus;
    last_address = address;
    last_timeout_ms = timeout_ms;
    if (poisoned) {
        return RTL8735B_BUS_TIMEOUT;
    }
    const int result = responses.empty() ? RTL8735B_BUS_OK : responses.front();
    if (!responses.empty()) {
        responses.pop_front();
    }
    if (result != RTL8735B_BUS_BUSY) {
        started_transactions++;
    }
    if (result == RTL8735B_BUS_TIMEOUT) {
        poisoned = true;
    }
    return result;
}

extern "C" int rtl8735b_spi_init(uint8_t, uint8_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, uint8_t) { return RTL8735B_BUS_ERROR; }
extern "C" int rtl8735b_spi_set_frequency(uint8_t, uint32_t) { return RTL8735B_BUS_ERROR; }
extern "C" int rtl8735b_spi_set_cs(uint8_t, uint32_t, uint8_t) { return RTL8735B_BUS_ERROR; }
extern "C" int rtl8735b_spi_transfer(uint8_t, uint32_t, const uint8_t *, uint32_t,
                                      uint8_t *, uint32_t, uint32_t, uint8_t)
{ return RTL8735B_BUS_ERROR; }
extern "C" int rtl8735b_spi_transfer_fullduplex(uint8_t, uint32_t, const uint8_t *,
                                                uint8_t *, uint32_t, uint32_t, uint8_t)
{ return RTL8735B_BUS_ERROR; }
extern "C" int rtl8735b_spi_clock_pulse(uint8_t, uint32_t, uint32_t)
{ return RTL8735B_BUS_ERROR; }

extern "C" {
#ifdef TEST_TWO_I2C_BUSES
// 板上感測器在 I2C1（內部），外接的在 I2C2（外部，D3、D4）
extern const RTL8735BI2CBusConfig RTL8735B_I2C_BUS_CONFIGS[] = {
    {1U, 1U, 42U, 41U, 400000U, 1U},
    {2U, 2U, 36U, 35U, 100000U, 0U},
};
extern const uint32_t RTL8735B_I2C_BUS_CONFIG_COUNT = 2U;
#else
extern const RTL8735BI2CBusConfig RTL8735B_I2C_BUS_CONFIGS[] = {
    {TEST_I2C_BUS, TEST_I2C_INDEX, 42U, 41U, 400000U, TEST_I2C_INTERNAL},
};
extern const uint32_t RTL8735B_I2C_BUS_CONFIG_COUNT = 1U;
#endif
extern const RTL8735BSPIBusConfig RTL8735B_SPI_BUS_CONFIGS[] = {
    {0U, 0U, 0U, 0U, 0U, 0U},
};
extern const uint32_t RTL8735B_SPI_BUS_CONFIG_COUNT = 0U;
extern const RTL8735BSPIDeviceConfig RTL8735B_SPI_DEVICE_CONFIGS[] = {
    {nullptr, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
};
extern const uint32_t RTL8735B_SPI_DEVICE_CONFIG_COUNT = 0U;
}

static void clear_transport()
{
    init_calls = 0U;
    initialized_bus = 0U;
    initialized_sda = initialized_scl = initialized_clock = 0U;
    transfer_calls = started_transactions = 0U;
    last_address = 0U;
    last_timeout_ms = 0U;
    poisoned = false;
    responses.clear();
}

#ifdef TEST_TWO_I2C_BUSES
// 兩條匯流排：各自初始化、各有自己的 bus task 與號誌；遮罩依各條的內外部；
// 一條初始化失敗時，另一條照常可用。
static void test_two_buses()
{
    clear_transport();
    RTL8735B::I2CDeviceManager manager;
    assert(init_calls == 2U);
    assert(manager.get_bus_mask() == ((1U << 1) | (1U << 2)));
    assert(manager.get_bus_mask_internal() == (1U << 1));
    assert(manager.get_bus_mask_external() == (1U << 2));

    // 時脈依各條的表；外接那條是 100 kHz
    assert(manager.get_device_ptr(2U, 0x1EU, 400000U) == nullptr);
    auto *baro = static_cast<RTL8735B::I2CDevice *>(manager.get_device_ptr(1U, 0x76U, 400000U));
    auto *ext = static_cast<RTL8735B::I2CDevice *>(manager.get_device_ptr(2U, 0x1EU, 100000U));
    assert(baro != nullptr && ext != nullptr);
    assert(baro->bus_num() == 1U && ext->bus_num() == 2U);
    // 兩條各有自己的號誌與 task：一條卡住不會擋住另一條
    assert(baro->get_semaphore() != ext->get_semaphore());

    const uint8_t command = 0xD0U;
    assert(ext->transfer(&command, 1U, nullptr, 0U) && last_bus == 2U && last_address == 0x1EU);
    assert(baro->transfer(&command, 1U, nullptr, 0U) && last_bus == 1U && last_address == 0x76U);

    created_task_count = 0U;
    assert(baro->register_periodic_callback(1000U, []() {}) != nullptr);
    assert(ext->register_periodic_callback(1000U, []() {}) != nullptr);
    assert(created_task_count == 2U);
    assert(std::string(created_tasks[0]) == "APM_I2C1" && std::string(created_tasks[1]) == "APM_I2C2");
    delete baro;
    delete ext;

    // I2C1 初始化失敗：只剩 I2C2
    clear_transport();
    fail_init_bus = 1U;
    RTL8735B::I2CDeviceManager partial;
    fail_init_bus = 0xFFU;
    assert(init_calls == 2U);
    assert(partial.get_bus_mask() == (1U << 2) && partial.get_bus_mask_internal() == 0U);
    assert(partial.get_device_ptr(1U, 0x76U, 400000U) == nullptr);
    AP_HAL::I2CDevice *still = partial.get_device_ptr(2U, 0x1EU, 100000U);
    assert(still != nullptr);
    delete still;
}
#endif

int main()
{
#ifdef TEST_TWO_I2C_BUSES
    test_two_buses();
    return 0;
#endif
    clear_transport();
    RTL8735B::I2CDeviceManager manager;
    assert(init_calls == 1U);
    assert(initialized_bus == TEST_I2C_BUS && initialized_sdk_index == TEST_I2C_INDEX);
    assert(initialized_sda == 42U && initialized_scl == 41U);
    assert(initialized_clock == 400000U);
    // 匯流排遮罩依表的邏輯編號，內外部依表的 internal 欄
    assert(manager.get_bus_mask() == (1U << TEST_I2C_BUS));
    assert(manager.get_bus_mask_external() == (TEST_I2C_INTERNAL ? 0U : (1U << TEST_I2C_BUS)));
    assert(manager.get_bus_mask_internal() == (TEST_I2C_INTERNAL ? (1U << TEST_I2C_BUS) : 0U));

    assert(manager.get_device_ptr(0U, 0x76U) == nullptr);
    assert(manager.get_device_ptr(2U, 0x76U) == nullptr);
    assert(manager.get_device_ptr(TEST_I2C_BUS, 0x80U) == nullptr);
    assert(manager.get_device_ptr(TEST_I2C_BUS, 0x76U, 100000U) == nullptr);
    assert(manager.get_device_ptr(TEST_I2C_BUS, 0x76U, 400000U, true) == nullptr);
    assert(transfer_calls == 0U);

    AP_HAL::I2CDevice *base = manager.get_device_ptr(TEST_I2C_BUS, 0x76U, 400000U, false, 4U);
    assert(base != nullptr);
    auto *device = static_cast<RTL8735B::I2CDevice *>(base);
    uint8_t command = 0xD0U;

    device->set_retries(0U);
    responses = {RTL8735B_BUS_ERROR};
    assert(!device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 1U && started_transactions == 1U);

    clear_transport();
    // 傳輸錯誤代表本次 slave address NACK；依 caller retries 再嘗試後可成功。
    responses = {RTL8735B_BUS_ERROR, RTL8735B_BUS_OK};
    device->set_retries(1U);
    assert(device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 2U && last_address == 0x76U && last_timeout_ms == 4U);

    clear_transport();
    for (uint8_t i = 0; i < 10U; i++) {
        responses.push_back(RTL8735B_BUS_BUSY);
    }
    responses.push_back(RTL8735B_BUS_OK);
    device->set_retries(10U);
    assert(device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 11U && started_transactions == 1U);

    clear_transport();
    for (uint8_t i = 0; i < 10U; i++) {
        responses.push_back(RTL8735B_BUS_ERROR);
    }
    responses.push_back(RTL8735B_BUS_OK);
    device->set_retries(10U);
    assert(device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 11U && started_transactions == 11U);

    clear_transport();
    for (uint16_t i = 0; i < 256U; i++) {
        responses.push_back(RTL8735B_BUS_ERROR);
    }
    device->set_retries(255U);
    assert(!device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 256U && started_transactions == 256U);

    clear_transport();
    responses = {RTL8735B_BUS_INVALID, RTL8735B_BUS_OK};
    device->set_retries(10U);
    assert(!device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 1U && started_transactions == 1U);

    clear_transport();
    responses = {RTL8735B_BUS_TIMEOUT};
    device->set_retries(10U);
    assert(!device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 1U && started_transactions == 1U);
    assert(!device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 2U && started_transactions == 1U);
    delete device;
}
