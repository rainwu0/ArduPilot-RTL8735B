#pragma once

// Scheduler 主機測試用的 AP_HAL 替身：只放 Scheduler.cpp 與 system.cpp 用到的部分。
#include <stddef.h>
#include <stdint.h>

#include "AP_HAL_Boards.h"
#include "Semaphores.h"
#include "RCInput.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

namespace AP_HAL {

void panic(const char *errormsg, ...) __attribute__((format(printf, 1, 2)));
uint32_t micros();
uint32_t millis();
uint64_t micros64();
uint64_t millis64();

typedef void (*Proc)(void);

// 正式的 MemberProc 是 Functor（可平凡複製；Scheduler.cpp 以 calloc 的記憶體保存它）
struct MemberProc {
    void (*fn)(void *);
    void *obj;
    void operator()() const { fn(obj); }
    bool operator==(const MemberProc &rhs) const { return fn == rhs.fn && obj == rhs.obj; }
    explicit operator bool() const { return fn != nullptr; }
};

class UARTDriver {
public:
    void _timer_tick() {}
};

class RCOutput {
public:
    void force_safety_on() {}
};

class Storage {
public:
    void _timer_tick() {}
};

// 介面照 libraries/AP_HAL/Scheduler.h；delay_microseconds_boost 的預設實作與正式基底類別相同
// （直接轉呼叫 delay_microseconds，AP_HAL/Scheduler.h:30），AP_InertialSensor::wait_for_sample 走這條。
class Scheduler {
public:
    virtual ~Scheduler() {}
    virtual void init() = 0;
    virtual void delay(uint16_t ms) = 0;
    virtual void delay_microseconds(uint16_t us) = 0;
    virtual void delay_microseconds_boost(uint16_t us) { delay_microseconds(us); }
    virtual void register_timer_process(MemberProc) = 0;
    virtual void register_io_process(MemberProc) = 0;
    virtual void register_timer_failsafe(Proc, uint32_t period_us) = 0;
    virtual void set_system_initialized() = 0;
    virtual bool is_system_initialized() = 0;
    virtual void reboot(bool hold_in_bootloader = false) = 0;
    virtual bool in_main_thread() const = 0;
    // 正式基底類別的預設實作（AP_HAL/Scheduler.h:38、44）；本 HAL 的 Scheduler 會覆寫
    virtual void expect_delay_ms(uint32_t) { }
    virtual bool in_expected_delay(void) const { return false; }
    virtual void call_delay_cb();
    uint16_t _min_delay_cb_ms;

    enum priority_base {
        PRIORITY_BOOST,
        PRIORITY_MAIN,
        PRIORITY_SPI,
        PRIORITY_I2C,
        PRIORITY_CAN,
        PRIORITY_TIMER,
        PRIORITY_RCOUT,
        PRIORITY_LED,
        PRIORITY_RCIN,
        PRIORITY_IO,
        PRIORITY_UART,
        PRIORITY_STORAGE,
        PRIORITY_SCRIPTING,
        PRIORITY_NET,
    };

    virtual bool thread_create(MemberProc, const char *, uint32_t, priority_base, int8_t) { return false; }
};

class HAL {
public:
    RCOutput *rcout;
    RCInput *rcin;
    Storage *storage;
    uint8_t num_serial;
    UARTDriver *serial(uint8_t) const { return nullptr; }
};

} // namespace AP_HAL
