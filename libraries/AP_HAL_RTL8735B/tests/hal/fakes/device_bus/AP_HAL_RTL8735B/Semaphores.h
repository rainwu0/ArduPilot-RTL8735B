#pragma once
#include <mutex>
#include <thread>
#include <AP_HAL/Device.h>
namespace RTL8735B {
class Semaphore : public AP_HAL::Semaphore {
public:
    void take_blocking() { _mutex.lock(); _owner = std::this_thread::get_id(); _depth++; }
    bool take_nonblocking()
    {
        if (!_mutex.try_lock()) { return false; }
        _owner = std::this_thread::get_id();
        _depth++;
        return true;
    }
    bool give() { if (_depth > 0U) { _depth--; } _mutex.unlock(); return true; }
    bool check_owner() const { return _depth > 0U && _owner == std::this_thread::get_id(); }
private:
    std::recursive_mutex _mutex;
    std::thread::id _owner;
    uint32_t _depth = 0;
};
}
