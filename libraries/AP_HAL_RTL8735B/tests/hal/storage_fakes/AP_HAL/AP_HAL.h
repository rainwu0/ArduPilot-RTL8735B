#pragma once
// 主機測試用的最小 AP_HAL：只提供 Storage 與 AP_FlashStorage 用到的部分。
#include <stddef.h>
#include <stdint.h>
#include <AP_HAL/utility/functor.h>
#include <AP_HAL/AP_HAL_Boards.h>
#include <AP_HAL/Storage.h>

#define CONFIG_HAL_BOARD 1
#define HAL_BOARD_RTL8735B 1
#define PACKED __attribute__((__packed__))
#define WARN_IF_UNUSED __attribute__((warn_unused_result))

namespace AP_HAL {
void panic(const char *, ...);
uint32_t millis();

class Util {
public:
    bool get_soft_armed() const { return soft_armed; }
    bool soft_armed = false;
};

class HAL {
public:
    Util *util;
};
}
