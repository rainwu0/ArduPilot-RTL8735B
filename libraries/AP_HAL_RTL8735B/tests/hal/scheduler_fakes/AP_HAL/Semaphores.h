#pragma once

#include <stdint.h>

namespace AP_HAL {

class Semaphore {
public:
    virtual ~Semaphore() {}
    virtual bool take(uint32_t timeout_ms) = 0;
    virtual bool take_nonblocking() = 0;
    virtual void take_blocking() = 0;
    virtual bool give() = 0;
};

class BinarySemaphore {
public:
    virtual ~BinarySemaphore() {}
    virtual bool wait(uint32_t timeout_us) = 0;
    virtual bool wait_blocking() = 0;
    virtual void signal() = 0;
    virtual void signal_ISR() = 0;
};

} // namespace AP_HAL

// 單執行緒測試不需要真的取得 semaphore
#define WITH_SEMAPHORE(sem) (void)(sem)
