/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <AP_HAL/I2CDevice.h>
#include "DeviceBus.h"

namespace RTL8735B {

class I2CDevice : public AP_HAL::I2CDevice {
public:
    I2CDevice(DeviceBus &bus, uint8_t bus_num, uint8_t address,
              uint32_t timeout_ms);

    void set_address(uint8_t address) override;
    void set_retries(uint8_t retries) override;
    bool set_speed(Device::Speed speed) override;
    bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;
    bool read_registers_multiple(uint8_t first_reg, uint8_t *recv,
                                 uint32_t recv_len, uint8_t times) override;
    AP_HAL::Semaphore *get_semaphore() override;
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb cb) override;
    bool adjust_periodic_callback(AP_HAL::Device::PeriodicHandle handle,
                                  uint32_t period_usec) override;
    bool unregister_callback(AP_HAL::Device::PeriodicHandle handle) override;
    void set_split_transfers(bool split) override;

private:
    DeviceBus &_bus;
    uint32_t _timeout_ms;
    uint8_t _address;
    uint8_t _retries;
    bool _split_transfers;
};

class I2CDeviceManager : public AP_HAL::I2CDeviceManager {
public:
    I2CDeviceManager();
    AP_HAL::I2CDevice *get_device_ptr(uint8_t bus, uint8_t address,
                                      uint32_t bus_clock=400000,
                                      bool use_smbus=false,
                                      uint32_t timeout_ms=4) override;
    uint32_t get_bus_mask(void) const override;
    uint32_t get_bus_mask_external(void) const override;
    uint32_t get_bus_mask_internal(void) const override;

private:
    bool _ready;
    DeviceBus _bus;
};

}
