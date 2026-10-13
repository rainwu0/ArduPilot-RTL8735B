#pragma once
#include <functional>
#include <stdint.h>
namespace AP_HAL {
class Device {
public:
    enum BusType { BUS_TYPE_UNKNOWN, BUS_TYPE_I2C, BUS_TYPE_SPI };
    enum Speed { SPEED_HIGH, SPEED_LOW };
    using PeriodicCb = std::function<void()>;
    using PeriodicHandle = void *;
    explicit Device(BusType type = BUS_TYPE_UNKNOWN) : _bus_type(type) {}
    virtual ~Device() = default;
    void set_device_bus(uint8_t bus) { _bus_num = bus; }
    void set_device_address(uint8_t address) { _address = address; }
    uint8_t bus_num() const { return _bus_num; }
    uint8_t get_bus_address() const { return _address; }
protected:
    BusType _bus_type;
    uint8_t _bus_num = 0;
    uint8_t _address = 0;
    uint8_t _read_flag = 0;
};
class Semaphore { public: virtual ~Semaphore() = default; };
}
