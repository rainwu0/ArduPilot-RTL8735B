/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_HAL/AP_HAL.h>
#include "BusConfig.h"
#include "I2CDevice.h"
#include "bus_shim.h"

#include <new>

using namespace RTL8735B;

namespace {
const RTL8735BI2CBusConfig *find_i2c_bus(uint8_t bus)
{
    for (uint32_t i = 0; i < RTL8735B_I2C_BUS_CONFIG_COUNT; i++) {
        if (RTL8735B_I2C_BUS_CONFIGS[i].logical_bus == bus) {
            return &RTL8735B_I2C_BUS_CONFIGS[i];
        }
    }
    return nullptr;
}
}

I2CDevice::I2CDevice(DeviceBus &bus, uint8_t bus_num, uint8_t address,
                     uint32_t timeout_ms) :
    AP_HAL::I2CDevice(),
    _bus(bus),
    _timeout_ms(timeout_ms),
    _address(address),
    _retries(0),
    _split_transfers(false)
{
    set_device_bus(bus_num);
    set_device_address(address);
}

void I2CDevice::set_address(uint8_t address)
{
    if (address <= 0x7FU) {
        _address = address;
        set_device_address(address);
    }
}

void I2CDevice::set_retries(uint8_t retries)
{
    _retries = retries;
}

bool I2CDevice::set_speed(AP_HAL::Device::Speed)
{
    // 控制器使用板表設定的固定頻率，所有裝置共用。
    return true;
}

bool I2CDevice::transfer(const uint8_t *send, uint32_t send_len,
                         uint8_t *recv, uint32_t recv_len)
{
    if ((send_len > 0U && send == nullptr) || (recv_len > 0U && recv == nullptr) ||
        (send_len + recv_len == 0U)) {
        return false;
    }
    _bus.semaphore.take_blocking();
    int result = RTL8735B_BUS_ERROR;
    // timeout_ms 為每個 leg（寫、讀）的上限，不縮減 caller 設定的 retry 次數。
    // timeout_ms 最大 100 ms、RTOS tick 1 ms，寫後讀交易最壞約 2 x 101 ms，uint8_t retry 最壞總上限為 256 倍。
    // BUS_TIMEOUT 不重試；shim 已封鎖該控制器以保護仍被 SDK 使用的靜態緩衝。
    for (uint16_t attempt = 0; attempt <= _retries; attempt++) {
        result = rtl8735b_i2c_transfer(bus_num(), _address,
                                        send, send_len, recv, recv_len,
                                        _split_transfers, _timeout_ms);
        if (result != RTL8735B_BUS_BUSY && result != RTL8735B_BUS_ERROR) {
            break;
        }
    }
    _bus.semaphore.give();
    return result == RTL8735B_BUS_OK;
}

bool I2CDevice::read_registers_multiple(uint8_t first_reg, uint8_t *recv,
                                         uint32_t recv_len, uint8_t times)
{
    if (recv == nullptr || recv_len == 0U || times == 0U ||
        recv_len > UINT32_MAX / times) {
        return false;
    }
    _bus.semaphore.take_blocking();
    bool success = true;
    const uint8_t reg = first_reg | _read_flag;
    for (uint8_t i = 0; i < times && success; i++) {
        success = transfer(&reg, 1U, recv + (i * recv_len), recv_len);
    }
    _bus.semaphore.give();
    return success;
}

AP_HAL::Semaphore *I2CDevice::get_semaphore()
{
    return &_bus.semaphore;
}

AP_HAL::Device::PeriodicHandle I2CDevice::register_periodic_callback(
    uint32_t period_usec, AP_HAL::Device::PeriodicCb cb)
{
    return _bus.register_periodic_callback(period_usec, cb);
}

bool I2CDevice::adjust_periodic_callback(
    AP_HAL::Device::PeriodicHandle handle, uint32_t period_usec)
{
    return _bus.adjust_periodic_callback(handle, period_usec);
}

bool I2CDevice::unregister_callback(AP_HAL::Device::PeriodicHandle handle)
{
    return _bus.unregister_callback(handle);
}

void I2CDevice::set_split_transfers(bool split)
{
    _split_transfers = split;
}

I2CDeviceManager::I2CDeviceManager() :
    _ready(false),
    _bus("APM_I2C1")
{
    const RTL8735BI2CBusConfig *cfg = find_i2c_bus(1U);
    if (cfg != nullptr) {
        _ready = rtl8735b_i2c_init(cfg->logical_bus, cfg->sda_pin,
                                    cfg->scl_pin, cfg->clock_hz) == RTL8735B_BUS_OK;
    }
}

AP_HAL::I2CDevice *I2CDeviceManager::get_device_ptr(
    uint8_t bus, uint8_t address, uint32_t bus_clock, bool use_smbus,
    uint32_t timeout_ms)
{
    const RTL8735BI2CBusConfig *cfg = find_i2c_bus(bus);
    if (!_ready || cfg == nullptr || address > 0x7FU ||
        bus_clock != cfg->clock_hz || use_smbus ||
        timeout_ms == 0U || timeout_ms > 100U) {
        return nullptr;
    }
    return new (std::nothrow) I2CDevice(_bus, bus, address, timeout_ms);
}

uint32_t I2CDeviceManager::get_bus_mask(void) const
{
    return _ready ? (1U << 1) : 0U;
}

uint32_t I2CDeviceManager::get_bus_mask_external(void) const
{
    return get_bus_mask();
}

uint32_t I2CDeviceManager::get_bus_mask_internal(void) const
{
    return 0U;
}
