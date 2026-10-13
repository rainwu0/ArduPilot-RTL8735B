#pragma once

#include <mutex>
#include <stdint.h>
#include <atomic>
#include <functional>

namespace RTL8735B {

class Semaphore {
public:
    static inline bool refuse_nonblocking = false;
    static inline std::atomic<uint32_t> blocking_calls{0};
    static inline thread_local std::function<void()> before_block;
    void take_blocking()
    {
        blocking_calls++;
        if (before_block) {
            auto hook = before_block;
            before_block = nullptr;
            hook();
        }
        _mutex.lock();
    }
    bool take_nonblocking() { return !refuse_nonblocking && _mutex.try_lock(); }
    void give() { _mutex.unlock(); }

private:
    std::recursive_mutex _mutex;
};

class SemaphoreGuard {
public:
    explicit SemaphoreGuard(Semaphore &sem) : _sem(sem) { _sem.take_blocking(); }
    ~SemaphoreGuard() { _sem.give(); }
private:
    Semaphore &_sem;
};

}

#define UART_TEST_JOIN_IMPL(a, b) a ## b
#define UART_TEST_JOIN(a, b) UART_TEST_JOIN_IMPL(a, b)
#define WITH_SEMAPHORE(sem) RTL8735B::SemaphoreGuard UART_TEST_JOIN(guard, __LINE__)(sem)
