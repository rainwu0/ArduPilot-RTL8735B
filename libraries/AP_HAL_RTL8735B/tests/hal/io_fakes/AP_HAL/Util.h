#pragma once
namespace AP_HAL {
class Util {
public:
    enum safety_state : unsigned char {
        SAFETY_NONE,
        SAFETY_DISARMED,
        SAFETY_ARMED,
    };
    bool get_soft_armed() const { return soft_armed; }
    bool soft_armed = false;
};
}
