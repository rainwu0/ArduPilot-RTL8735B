/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <AP_HAL/SPIDevice.h>
#include "BusConfig.h"
#include "DeviceBus.h"

namespace RTL8735B {

class SPIDevice : public AP_HAL::SPIDevice {
public:
    SPIDevice(DeviceBus &bus, const RTL8735BSPIDeviceConfig &config);
    ~SPIDevice() override;

    bool set_speed(AP_HAL::Device::Speed speed) override;
    bool transfer(const uint8_t *send, uint32_t send_len,
                  uint8_t *recv, uint32_t recv_len) override;
    bool transfer_fullduplex(const uint8_t *send, uint8_t *recv,
                             uint32_t len) override;
    bool transfer_fullduplex(uint8_t *send_recv, uint32_t len) override;
    bool clock_pulse(uint32_t len) override;
    AP_HAL::Semaphore *get_semaphore() override;
    AP_HAL::Device::PeriodicHandle register_periodic_callback(
        uint32_t period_usec, AP_HAL::Device::PeriodicCb cb) override;
    bool adjust_periodic_callback(AP_HAL::Device::PeriodicHandle handle,
                                  uint32_t period_usec) override;
    bool unregister_callback(AP_HAL::Device::PeriodicHandle handle) override;
    bool set_chip_select(bool set) override;

private:
    bool _begin_transfer(bool &took_lock);
    void _end_transfer(bool took_lock, bool success);

    DeviceBus &_bus;
    const RTL8735BSPIDeviceConfig &_config;
    AP_HAL::Device::Speed _speed;
    bool _manual_cs;
};

class SPIDeviceManager : public AP_HAL::SPIDeviceManager {
public:
    SPIDeviceManager();
    AP_HAL::SPIDevice *get_device_ptr(const char *name) override;
    uint8_t get_count() override;
    const char *get_device_name(uint8_t idx) override;

private:
    bool _ready;
    DeviceBus _bus;
};

}
