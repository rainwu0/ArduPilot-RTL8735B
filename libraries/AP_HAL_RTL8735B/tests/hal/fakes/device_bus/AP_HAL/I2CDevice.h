#pragma once
#include "Device.h"
namespace AP_HAL {
class I2CDevice : public Device {
public:
    I2CDevice() : Device(BUS_TYPE_I2C) {}
    virtual void set_address(uint8_t) {}
    virtual void set_retries(uint8_t) {}
    virtual bool set_speed(Speed) = 0;
    virtual bool transfer(const uint8_t *, uint32_t, uint8_t *, uint32_t) = 0;
    virtual bool read_registers_multiple(uint8_t, uint8_t *, uint32_t, uint8_t) = 0;
    virtual Semaphore *get_semaphore() = 0;
    virtual PeriodicHandle register_periodic_callback(uint32_t, PeriodicCb) = 0;
    virtual bool adjust_periodic_callback(PeriodicHandle, uint32_t) = 0;
    virtual bool unregister_callback(PeriodicHandle) { return false; }
    virtual void set_split_transfers(bool) {}
};

class I2CDeviceManager {
public:
    virtual ~I2CDeviceManager() = default;
    virtual I2CDevice *get_device_ptr(uint8_t bus, uint8_t address,
                                     uint32_t bus_clock = 400000,
                                     bool use_smbus = false,
                                     uint32_t timeout_ms = 4) = 0;
    virtual uint32_t get_bus_mask() const { return 0x0FU; }
    virtual uint32_t get_bus_mask_external() const { return 0x0FU; }
    virtual uint32_t get_bus_mask_internal() const { return 0x01U; }
};
}
