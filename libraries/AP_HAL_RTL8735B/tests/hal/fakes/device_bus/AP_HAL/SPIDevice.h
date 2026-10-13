#pragma once
#include "Device.h"
namespace AP_HAL {
class SPIDevice : public Device {
public:
    SPIDevice() : Device(BUS_TYPE_SPI) {}
    virtual bool set_speed(Speed) = 0;
    virtual bool transfer(const uint8_t *, uint32_t, uint8_t *, uint32_t) = 0;
    virtual bool transfer_fullduplex(const uint8_t *, uint8_t *, uint32_t) = 0;
    virtual bool transfer_fullduplex(uint8_t *send_recv, uint32_t len)
    {
        return transfer_fullduplex(send_recv, send_recv, len);
    }
    virtual bool clock_pulse(uint32_t) { return false; }
    virtual Semaphore *get_semaphore() = 0;
    virtual PeriodicHandle register_periodic_callback(uint32_t, PeriodicCb) = 0;
    virtual bool adjust_periodic_callback(PeriodicHandle, uint32_t) = 0;
    virtual bool unregister_callback(PeriodicHandle) { return false; }
    virtual bool set_chip_select(bool) { return false; }
};
class SPIDeviceManager {
public:
    virtual ~SPIDeviceManager() = default;
    virtual SPIDevice *get_device_ptr(const char *) = 0;
    virtual uint8_t get_count() { return 0; }
    virtual const char *get_device_name(uint8_t) { return nullptr; }
};
}
