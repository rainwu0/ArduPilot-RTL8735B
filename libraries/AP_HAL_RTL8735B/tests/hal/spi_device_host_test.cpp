/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "SPIDevice.h"
#include "BusConfig.h"
#include "bus_shim.h"
#include <AP_HAL/AP_HAL.h>

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {
uint64_t now_us;
uint8_t init_bus;
uint8_t init_sdk_index;
uint32_t init_mosi;
uint32_t init_miso;
uint32_t init_sclk;
uint32_t init_ss;
uint32_t init_clock;
uint8_t init_mode;
uint8_t init_calls;
uint32_t selected_cs;
uint8_t selected_cs_asserted;
uint32_t selected_frequency;
uint32_t transfer_calls;
uint32_t transfer_cs;
uint32_t transfer_timeout;
uint8_t transfer_kept_cs;
uint32_t transfer_len;
uint8_t transfer_tx[4096];
// 目前的 SPI mode、改過幾次，以及傳輸與拉 CS 當下的 mode
uint8_t current_mode = 0xFFU;
uint32_t set_mode_calls;
uint8_t transfer_mode;
uint8_t cs_assert_mode;
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
extern "C" TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return reinterpret_cast<TaskHandle_t>(2U);
}

namespace AP_HAL {
uint64_t micros64() { return now_us; }
}

extern "C" int rtl8735b_spi_init(uint8_t bus, uint8_t sdk_index, uint32_t mosi, uint32_t miso,
                                   uint32_t sclk, uint32_t ss, uint32_t clock_hz,
                                   uint8_t mode)
{
    init_calls++;
    init_bus = bus;
    init_sdk_index = sdk_index;
    init_mosi = mosi;
    init_miso = miso;
    init_sclk = sclk;
    init_ss = ss;
    init_clock = clock_hz;
    init_mode = mode;
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_spi_set_frequency(uint8_t, uint32_t clock_hz)
{
    selected_frequency = clock_hz;
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_spi_set_mode(uint8_t, uint8_t mode)
{
    set_mode_calls++;
    current_mode = mode;
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_spi_set_cs(uint8_t, uint32_t cs_pin, uint8_t asserted)
{
    if (asserted) {
        cs_assert_mode = current_mode;
    }
    selected_cs = cs_pin;
    selected_cs_asserted = asserted;
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_spi_transfer(uint8_t, uint32_t cs_pin,
                                       const uint8_t *send, uint32_t send_len,
                                       uint8_t *, uint32_t, uint32_t timeout_ms,
                                       uint8_t keep_cs)
{
    transfer_calls++;
    transfer_mode = current_mode;
    transfer_cs = cs_pin;
    transfer_timeout = timeout_ms;
    transfer_kept_cs = keep_cs;
    transfer_len = send_len;
    if (send_len > 0U) {
        memcpy(transfer_tx, send, send_len);
    }
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_spi_transfer_fullduplex(
    uint8_t, uint32_t cs_pin, const uint8_t *send, uint8_t *recv,
    uint32_t len, uint32_t timeout_ms, uint8_t keep_cs)
{
    transfer_calls++;
    transfer_mode = current_mode;
    transfer_cs = cs_pin;
    transfer_timeout = timeout_ms;
    transfer_kept_cs = keep_cs;
    transfer_len = len;
    uint8_t tx_copy[4096];
    memcpy(tx_copy, send, len);
    memcpy(transfer_tx, tx_copy, len);
    for (uint32_t i = 0; i < len; i++) {
        recv[i] = static_cast<uint8_t>(tx_copy[i] ^ 0xFFU);
    }
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_spi_clock_pulse(uint8_t, uint32_t, uint32_t)
{
    return RTL8735B_BUS_OK;
}

extern "C" int rtl8735b_i2c_init(uint8_t, uint8_t, uint32_t, uint32_t, uint32_t)
{
    return RTL8735B_BUS_ERROR;
}
extern "C" int rtl8735b_i2c_transfer(uint8_t, uint8_t, const uint8_t *, uint32_t,
                                       uint8_t *, uint32_t, uint8_t, uint32_t)
{
    return RTL8735B_BUS_ERROR;
}

extern "C" {
extern const RTL8735BI2CBusConfig RTL8735B_I2C_BUS_CONFIGS[] = {
    {0U, 0U, 0U, 0U, 0U, 0U},
};
extern const uint32_t RTL8735B_I2C_BUS_CONFIG_COUNT = 0U;
extern const RTL8735BSPIBusConfig RTL8735B_SPI_BUS_CONFIGS[] = {
    {1U, 0U, 47U, 45U, 46U, 1000000U},
};
extern const uint32_t RTL8735B_SPI_BUS_CONFIG_COUNT = 1U;
extern const RTL8735BSPIDeviceConfig RTL8735B_SPI_DEVICE_CONFIGS[] = {
    {"bmi160", 1U, 88U, 1000000U, 10000000U, 20U, 17U, 0U},
    // 同一條匯流排上的第二個裝置：mode、片選與裝置識別碼都和第一個不同
    {"icm20689", 1U, 89U, 1000000U, 8000000U, 20U, 18U, 3U},
};
extern const uint32_t RTL8735B_SPI_DEVICE_CONFIG_COUNT = 2U;
}

// 同一條匯流排上有 mode 不同的裝置：每次傳輸當下、手動拉片選當下，SPI 的 mode 與片選
// 都要是這個裝置自己的，不能沿用上一個用過匯流排的裝置，也不能一直是初始化時的 mode。
static void test_each_device_uses_its_own_mode_and_chip_select(RTL8735B::SPIDeviceManager &manager)
{
    auto *dev0 = static_cast<RTL8735B::SPIDevice *>(manager.get_device_ptr("bmi160"));
    auto *dev1 = static_cast<RTL8735B::SPIDevice *>(manager.get_device_ptr("icm20689"));
    assert(dev0 != nullptr && dev1 != nullptr);

    // 兩個裝置在同一條匯流排上，但裝置識別碼不同
    assert(dev0->bus_num() == dev1->bus_num());
    assert(dev0->get_bus_address() == 17U && dev1->get_bus_address() == 18U);
    assert(dev0->get_bus_address() != dev1->get_bus_address());

    // 兩個裝置交錯傳輸：一般傳輸與全雙工都涵蓋，每次都換回該裝置自己的 mode 與片選。
    const uint8_t command = 0x80U;
    uint8_t duplex[2] = {0x5AU, 0xA5U};
    assert(dev1->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_mode == 3U && transfer_cs == 89U);
    assert(dev0->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_mode == 0U && transfer_cs == 88U);
    assert(dev1->transfer_fullduplex(duplex, 2U));
    assert(transfer_mode == 3U && transfer_cs == 89U);
    assert(dev0->transfer_fullduplex(duplex, 2U));
    assert(transfer_mode == 0U && transfer_cs == 88U);
    assert(dev1->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_mode == 3U && transfer_cs == 89U);

    // 手動拉片選：匯流排上一個用的是另一個裝置，拉片選當下也要已經換成自己的 mode。
    assert(dev0->set_chip_select(true));
    assert(cs_assert_mode == 0U && selected_cs == 88U && selected_cs_asserted == 1U);
    // 片選持有期間的傳輸沿用自己的 mode，並且不放開片選。
    assert(dev0->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_mode == 0U && transfer_cs == 88U && transfer_kept_cs == 1U);
    assert(dev0->set_chip_select(false));
    assert(selected_cs == 88U && selected_cs_asserted == 0U);

    assert(dev1->set_chip_select(true));
    assert(cs_assert_mode == 3U && selected_cs == 89U && selected_cs_asserted == 1U);
    assert(dev1->transfer_fullduplex(duplex, 2U));
    assert(transfer_mode == 3U && transfer_cs == 89U && transfer_kept_cs == 1U);
    assert(dev1->set_chip_select(false));
    assert(selected_cs == 89U && selected_cs_asserted == 0U);

    delete dev0;
    delete dev1;
}

int main()
{
    RTL8735B::SPIDeviceManager manager;
    assert(init_calls == 1U);
    // SDK 的 SPI 編號來自表（這裡故意用 0，和邏輯匯流排 1 不同）
    assert(init_sdk_index == 0U);
    assert(init_bus == 1U && init_mosi == 47U && init_miso == 45U && init_sclk == 46U);
    // SSI 初始化需要真實 CS 腳；傳入 bus 上第一個裝置的片選腳。
    assert(init_ss == 88U);
    assert(init_clock == 1000000U && init_mode == 0U);
    assert(manager.get_count() == 2U);
    assert(std::strcmp(manager.get_device_name(0U), "bmi160") == 0);
    assert(std::strcmp(manager.get_device_name(1U), "icm20689") == 0);
    assert(manager.get_device_name(2U) == nullptr);
    assert(manager.get_device_ptr("unknown") == nullptr);

    auto *device = static_cast<RTL8735B::SPIDevice *>(manager.get_device_ptr("bmi160"));
    assert(device != nullptr);
    assert(device->bus_num() == 1U);
    assert(device->get_bus_address() == 17U);
    assert(device->set_speed(AP_HAL::Device::SPEED_HIGH));
    assert(selected_frequency == 10000000U);

    uint8_t full_duplex[] = {0x12U, 0xA5U};
    assert(device->transfer_fullduplex(full_duplex, 2U));
    assert(transfer_calls == 1U && transfer_len == 2U && transfer_cs == 88U);
    assert(transfer_timeout == 20U && transfer_kept_cs == 0U);
    assert(transfer_tx[0] == 0x12U && transfer_tx[1] == 0xA5U);
    assert(full_duplex[0] == 0xEDU && full_duplex[1] == 0x5AU);

    assert(device->set_chip_select(true));
    assert(selected_cs == 88U && selected_cs_asserted == 1U);
    const uint8_t command = 0x80U;
    assert(device->transfer(&command, 1U, nullptr, 0U));
    assert(transfer_calls == 2U && transfer_kept_cs == 1U);
    assert(device->set_chip_select(false));
    assert(selected_cs == 88U && selected_cs_asserted == 0U);
    delete device;

    test_each_device_uses_its_own_mode_and_chip_select(manager);
}
