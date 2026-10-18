#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
namespace AP_HAL {
class Util {
public:
    enum safety_state : unsigned char {
        SAFETY_NONE,
        SAFETY_DISARMED,
        SAFETY_ARMED,
    };
    bool get_soft_armed() const { return soft_armed; }
    int snprintf(char *str, size_t size, const char *format, ...) __attribute__((format(printf, 4, 5)))
    {
        va_list ap;
        va_start(ap, format);
        const int n = vsnprintf(str, size, format, ap);
        va_end(ap);
        return n;
    }
    bool soft_armed = false;
};
}
