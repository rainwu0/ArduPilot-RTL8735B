#pragma once
#include <stdint.h>
namespace AP_HAL {
class Semaphore {
public:
    virtual ~Semaphore() = default;
    virtual bool give() = 0;
    virtual bool take(uint32_t timeout_ms) = 0;
    virtual bool take_nonblocking() = 0;
    virtual void take_blocking() = 0;
};
class BinarySemaphore {
public:
    virtual ~BinarySemaphore() = default;
    virtual bool wait(uint32_t timeout_us) = 0;
    virtual bool wait_blocking() = 0;
    virtual void signal() = 0;
    virtual void signal_ISR() = 0;
};
}
