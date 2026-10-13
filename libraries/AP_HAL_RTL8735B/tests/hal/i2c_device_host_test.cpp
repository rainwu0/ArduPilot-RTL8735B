/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "I2CDevice.h"
#include "BusConfig.h"
#include "bus_shim.h"
#include <AP_HAL/AP_HAL.h>

#include <cassert>
#include <cstdint>
#include <deque>

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

extern "C" BaseType_t xTaskCreate(TaskFunction_t, const char *, uint32_t,
                                    void *, UBaseType_t, TaskHandle_t *)
{
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
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_i2c_transfer(uint8_t, uint8_t address,
                                       const uint8_t *, uint32_t,
                                       uint8_t *, uint32_t,
                                       uint8_t, uint32_t timeout_ms)
{
    transfer_calls++;
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
extern const RTL8735BI2CBusConfig RTL8735B_I2C_BUS_CONFIGS[] = {
    {TEST_I2C_BUS, TEST_I2C_INDEX, 42U, 41U, 400000U, TEST_I2C_INTERNAL},
};
extern const uint32_t RTL8735B_I2C_BUS_CONFIG_COUNT = 1U;
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

int main()
{
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
