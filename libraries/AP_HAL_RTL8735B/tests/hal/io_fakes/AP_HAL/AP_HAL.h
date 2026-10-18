#pragma once
#include <functional>
#include <stdint.h>
#include "Util.h"
namespace AP_HAL {
using MemberProc = std::function<void()>;
// 由使用到的測試定義
uint32_t micros();
class Scheduler {
public:
    void register_io_process(MemberProc proc) { _io_proc = proc; }
    void service_io() { if (_io_proc) { _io_proc(); } }
    void register_timer_process(MemberProc proc) { _timer_proc = proc; }
    bool has_timer_process() const { return static_cast<bool>(_timer_proc); }
    void run_timer() { if (_timer_proc) { _timer_proc(); } }
private:
    MemberProc _io_proc;
    MemberProc _timer_proc;
};
class HAL {
public:
    explicit HAL(Scheduler *sched) : scheduler(sched) {}
    Scheduler *scheduler;
    Util *util = nullptr;
};
}
#define FUNCTOR_BIND_MEMBER(method, rettype) [this]() { (this->*method)(); }
