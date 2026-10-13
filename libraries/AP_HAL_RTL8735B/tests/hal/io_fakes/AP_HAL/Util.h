#pragma once
namespace AP_HAL {
class Util {
public:
    enum safety_state : unsigned char {
        SAFETY_NONE,
        SAFETY_DISARMED,
        SAFETY_ARMED,
    };
};
}
