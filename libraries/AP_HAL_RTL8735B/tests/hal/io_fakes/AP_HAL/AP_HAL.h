#pragma once
#include <functional>
namespace AP_HAL {
using MemberProc = std::function<void()>;
class Scheduler {
public:
    void register_io_process(MemberProc proc) { _io_proc = proc; }
    void service_io() { if (_io_proc) { _io_proc(); } }
private:
    MemberProc _io_proc;
};
class HAL {
public:
    explicit HAL(Scheduler *sched) : scheduler(sched) {}
    Scheduler *scheduler;
};
}
#define FUNCTOR_BIND_MEMBER(method, rettype) [this]() { (this->*method)(); }
