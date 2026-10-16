/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_HAL/AP_HAL.h>
#include "BusConfig.h"
#include "I2CDevice.h"
#include "bus_shim.h"

#include <new>

using namespace RTL8735B;

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
    // BUS_TIMEOUT 不重試：shim 已重新初始化控制器（連續逾時達上限時封鎖），由驅動決定下一步。
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

namespace {
// hwdef 表中實際用到的匯流排數（產生器保證 1–RTL8735B_I2C_MAX_BUSES 條）
uint32_t i2c_bus_count()
{
    return RTL8735B_I2C_BUS_CONFIG_COUNT < RTL8735B_I2C_MAX_BUSES ?
        RTL8735B_I2C_BUS_CONFIG_COUNT : RTL8735B_I2C_MAX_BUSES;
}

// bus task 的名稱依 SDK 的控制器編號，例如 I2C1 是 APM_I2C1；產生器保證控制器編號不重複。
const char *bus_task_name(uint32_t slot)
{
    static const char *const names[] = {"APM_I2C0", "APM_I2C1", "APM_I2C2", "APM_I2C3"};
    if (slot < i2c_bus_count() && RTL8735B_I2C_BUS_CONFIGS[slot].sdk_index < 4U) {
        return names[RTL8735B_I2C_BUS_CONFIGS[slot].sdk_index];
    }
    return "APM_I2C";
}
}

static_assert(RTL8735B_I2C_MAX_BUSES == 4U, "I2CDeviceManager has one DeviceBus member per bus");

I2CDeviceManager::I2CDeviceManager() :
    _bus0(bus_task_name(0)),
    _bus1(bus_task_name(1)),
    _bus2(bus_task_name(2)),
    _bus3(bus_task_name(3)),
    _buses{&_bus0, &_bus1, &_bus2, &_bus3},
    _ready{false, false, false, false}
{
    // 每條各自初始化；一條失敗時其他條照常可用。
    for (uint32_t i = 0; i < i2c_bus_count(); i++) {
        const RTL8735BI2CBusConfig &cfg = RTL8735B_I2C_BUS_CONFIGS[i];
        _ready[i] = rtl8735b_i2c_init(cfg.logical_bus, cfg.sdk_index, cfg.sda_pin,
                                       cfg.scl_pin, cfg.clock_hz) == RTL8735B_BUS_OK;
    }
}

int I2CDeviceManager::find_slot(uint8_t bus) const
{
    for (uint32_t i = 0; i < i2c_bus_count(); i++) {
        if (RTL8735B_I2C_BUS_CONFIGS[i].logical_bus == bus) {
            return _ready[i] ? (int)i : -1;
        }
    }
    return -1;
}

AP_HAL::I2CDevice *I2CDeviceManager::get_device_ptr(
    uint8_t bus, uint8_t address, uint32_t bus_clock, bool use_smbus,
    uint32_t timeout_ms)
{
    const int slot = find_slot(bus);
    if (slot < 0 || address > 0x7FU ||
        bus_clock != RTL8735B_I2C_BUS_CONFIGS[slot].clock_hz || use_smbus ||
        timeout_ms == 0U || timeout_ms > 100U) {
        return nullptr;
    }
    return new (std::nothrow) I2CDevice(*_buses[slot], bus, address, timeout_ms);
}

// 已初始化的匯流排的遮罩；依表的 internal 欄只取內部或外部的
uint32_t I2CDeviceManager::mask_of(bool internal_only, bool external_only) const
{
    uint32_t mask = 0U;
    for (uint32_t i = 0; i < i2c_bus_count(); i++) {
        const RTL8735BI2CBusConfig &cfg = RTL8735B_I2C_BUS_CONFIGS[i];
        if (!_ready[i] || (internal_only && !cfg.internal) || (external_only && cfg.internal)) {
            continue;
        }
        mask |= 1UL << cfg.logical_bus;
    }
    return mask;
}

uint32_t I2CDeviceManager::get_bus_mask(void) const
{
    return mask_of(false, false);
}

uint32_t I2CDeviceManager::get_bus_mask_external(void) const
{
    return mask_of(false, true);
}

uint32_t I2CDeviceManager::get_bus_mask_internal(void) const
{
    return mask_of(true, false);
}
