/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_HAL/AP_HAL.h>
#include "SPIDevice.h"
#include "bus_shim.h"

#include <cstring>
#include <new>

using namespace RTL8735B;

namespace {
constexpr uint32_t MAX_SPI_TIMEOUT_MS = 100U;

const RTL8735BSPIBusConfig *find_spi_bus(uint8_t bus)
{
    for (uint32_t i = 0; i < RTL8735B_SPI_BUS_CONFIG_COUNT; i++) {
        if (RTL8735B_SPI_BUS_CONFIGS[i].logical_bus == bus) {
            return &RTL8735B_SPI_BUS_CONFIGS[i];
        }
    }
    return nullptr;
}

const RTL8735BSPIDeviceConfig *find_spi_device(const char *name)
{
    if (name == nullptr) {
        return nullptr;
    }
    for (uint32_t i = 0; i < RTL8735B_SPI_DEVICE_CONFIG_COUNT; i++) {
        if (strcmp(name, RTL8735B_SPI_DEVICE_CONFIGS[i].name) == 0) {
            return &RTL8735B_SPI_DEVICE_CONFIGS[i];
        }
    }
    return nullptr;
}
}

SPIDevice::SPIDevice(DeviceBus &bus,
                     const RTL8735BSPIDeviceConfig &config) :
    AP_HAL::SPIDevice(),
    _bus(bus),
    _config(config),
    _speed(AP_HAL::Device::SPEED_LOW),
    _manual_cs(false)
{
    set_device_bus(config.logical_bus);
    set_device_address(config.device_id);
}

SPIDevice::~SPIDevice()
{
    _bus.semaphore.take_blocking();
    if (_manual_cs) {
        rtl8735b_spi_set_cs(bus_num(), _config.chip_select_pin, 0U);
        _manual_cs = false;
        _bus.semaphore.give();
    }
    _bus.semaphore.give();
}

bool SPIDevice::set_speed(AP_HAL::Device::Speed speed)
{
    if (speed != AP_HAL::Device::SPEED_LOW && speed != AP_HAL::Device::SPEED_HIGH) {
        return false;
    }
    const uint32_t hz = speed == AP_HAL::Device::SPEED_HIGH ?
        _config.high_speed_hz : _config.low_speed_hz;
    if (hz == 0U) {
        return false;
    }
    _bus.semaphore.take_blocking();
    const bool success = rtl8735b_spi_set_frequency(bus_num(), hz) == RTL8735B_BUS_OK;
    if (success) {
        _speed = speed;
    }
    _bus.semaphore.give();
    return success;
}

bool SPIDevice::_begin_transfer(bool &took_lock)
{
    took_lock = true;
    _bus.semaphore.take_blocking();
    return true;
}

void SPIDevice::_end_transfer(bool took_lock, bool success)
{
    if (!success && _manual_cs && _bus.semaphore.check_owner()) {
        rtl8735b_spi_set_cs(bus_num(), _config.chip_select_pin, 0U);
        _manual_cs = false;
        _bus.semaphore.give();
    }
    if (took_lock) {
        _bus.semaphore.give();
    }
}

bool SPIDevice::transfer(const uint8_t *send, uint32_t send_len,
                         uint8_t *recv, uint32_t recv_len)
{
    if ((send_len == 0U && recv_len == 0U) ||
        (send_len > 0U && send == nullptr && recv_len == 0U && recv == nullptr) ||
        (send_len + recv_len > 4096U)) {
        return false;
    }

    uint8_t *fill_send = nullptr;
    uint8_t *discard_recv = nullptr;
    if (send_len > 0U && send == nullptr) {
        fill_send = new (std::nothrow) uint8_t[send_len];
        if (fill_send == nullptr) {
            return false;
        }
        memset(fill_send, 0xFF, send_len);
        send = fill_send;
    }
    if (recv_len > 0U && recv == nullptr) {
        discard_recv = new (std::nothrow) uint8_t[recv_len];
        if (discard_recv == nullptr) {
            delete[] fill_send;
            return false;
        }
        recv = discard_recv;
    }

    bool took_lock;
    if (!_begin_transfer(took_lock)) {
        delete[] fill_send;
        delete[] discard_recv;
        return false;
    }
    const bool success = rtl8735b_spi_transfer(
        bus_num(), _config.chip_select_pin, send, send_len, recv, recv_len,
        _config.timeout_ms, _manual_cs ? 1U : 0U) == RTL8735B_BUS_OK;
    _end_transfer(took_lock, success);
    delete[] fill_send;
    delete[] discard_recv;
    return success;
}

bool SPIDevice::transfer_fullduplex(const uint8_t *send, uint8_t *recv,
                                    uint32_t len)
{
    if (len == 0U || len > 4096U) {
        return false;
    }
    uint8_t *fill_send = nullptr;
    uint8_t *discard_recv = nullptr;
    if (send == nullptr) {
        fill_send = new (std::nothrow) uint8_t[len];
        if (fill_send == nullptr) {
            return false;
        }
        memset(fill_send, 0xFF, len);
        send = fill_send;
    }
    if (recv == nullptr) {
        discard_recv = new (std::nothrow) uint8_t[len];
        if (discard_recv == nullptr) {
            delete[] fill_send;
            return false;
        }
        recv = discard_recv;
    }
    bool took_lock;
    if (!_begin_transfer(took_lock)) {
        delete[] fill_send;
        delete[] discard_recv;
        return false;
    }
    const bool success = rtl8735b_spi_transfer_fullduplex(
        bus_num(), _config.chip_select_pin, send, recv, len,
        _config.timeout_ms, _manual_cs ? 1U : 0U) == RTL8735B_BUS_OK;
    _end_transfer(took_lock, success);
    delete[] fill_send;
    delete[] discard_recv;
    return success;
}

bool SPIDevice::transfer_fullduplex(uint8_t *send_recv, uint32_t len)
{
    return transfer_fullduplex(send_recv, send_recv, len);
}

bool SPIDevice::clock_pulse(uint32_t len)
{
    if (len == 0U) {
        return false;
    }
    _bus.semaphore.take_blocking();
    if (_manual_cs) {
        _bus.semaphore.give();
        return false;
    }
    const bool success = rtl8735b_spi_clock_pulse(
        bus_num(), len, _config.timeout_ms) == RTL8735B_BUS_OK;
    _bus.semaphore.give();
    return success;
}

AP_HAL::Semaphore *SPIDevice::get_semaphore()
{
    return &_bus.semaphore;
}

AP_HAL::Device::PeriodicHandle SPIDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    return _bus.register_periodic_callback(period_usec, cb);
}

bool SPIDevice::adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle handle, uint32_t period_usec)
{
    return _bus.adjust_periodic_callback(handle, period_usec);
}

bool SPIDevice::unregister_callback(AP_HAL::Device::PeriodicHandle handle)
{
    return _bus.unregister_callback(handle);
}

bool SPIDevice::set_chip_select(bool set)
{
    if (set) {
        if (!_bus.semaphore.take_nonblocking()) {
            return false;
        }
        if (_manual_cs) {
            _bus.semaphore.give();
            return false;
        }
        if (rtl8735b_spi_set_cs(bus_num(), _config.chip_select_pin, 1U) != RTL8735B_BUS_OK) {
            _bus.semaphore.give();
            return false;
        }
        _manual_cs = true;
        return true;
    }
    if (!_bus.semaphore.check_owner() || !_manual_cs) {
        return false;
    }
    const bool success = rtl8735b_spi_set_cs(bus_num(), _config.chip_select_pin, 0U) ==
        RTL8735B_BUS_OK;
    _manual_cs = false;
    _bus.semaphore.give();
    return success;
}

SPIDeviceManager::SPIDeviceManager() :
    _ready(false),
    _bus("APM_SPI1")
{
    if (RTL8735B_SPI_BUS_CONFIG_COUNT != 1U || RTL8735B_SPI_DEVICE_CONFIG_COUNT == 0U) {
        return;
    }
    const RTL8735BSPIBusConfig &bus = RTL8735B_SPI_BUS_CONFIGS[0];
    // SSI 初始化需要一隻可註冊的 CS 腳；借用第一個裝置的片選腳，shim 註冊後即交還 GPIO。
    _ready = rtl8735b_spi_init(bus.logical_bus, bus.mosi_pin, bus.miso_pin,
                                bus.sclk_pin, RTL8735B_SPI_DEVICE_CONFIGS[0].chip_select_pin,
                                bus.default_clock_hz,
                                RTL8735B_SPI_DEVICE_CONFIGS[0].mode) == RTL8735B_BUS_OK;
    for (uint32_t i = 0; i < RTL8735B_SPI_DEVICE_CONFIG_COUNT && _ready; i++) {
        _ready = RTL8735B_SPI_DEVICE_CONFIGS[i].logical_bus == bus.logical_bus &&
            RTL8735B_SPI_DEVICE_CONFIGS[i].timeout_ms > 0U &&
            RTL8735B_SPI_DEVICE_CONFIGS[i].timeout_ms <= MAX_SPI_TIMEOUT_MS &&
            rtl8735b_spi_set_cs(bus.logical_bus,
                RTL8735B_SPI_DEVICE_CONFIGS[i].chip_select_pin, 0U) == RTL8735B_BUS_OK;
    }
}

AP_HAL::SPIDevice *SPIDeviceManager::get_device_ptr(const char *name)
{
    const RTL8735BSPIDeviceConfig *config = find_spi_device(name);
    if (!_ready || config == nullptr || find_spi_bus(config->logical_bus) == nullptr ||
        config->mode > 3U || config->low_speed_hz == 0U ||
        config->high_speed_hz == 0U) {
        return nullptr;
    }
    return new (std::nothrow) SPIDevice(_bus, *config);
}

uint8_t SPIDeviceManager::get_count()
{
    return _ready ? static_cast<uint8_t>(RTL8735B_SPI_DEVICE_CONFIG_COUNT) : 0U;
}

const char *SPIDeviceManager::get_device_name(uint8_t idx)
{
    return _ready && idx < RTL8735B_SPI_DEVICE_CONFIG_COUNT ?
        RTL8735B_SPI_DEVICE_CONFIGS[idx].name : nullptr;
}
