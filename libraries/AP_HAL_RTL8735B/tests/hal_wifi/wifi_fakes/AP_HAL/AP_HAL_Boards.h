#pragma once

#include <new>

#define HAL_BOARD_RTL8735B 1
#define CONFIG_HAL_BOARD HAL_BOARD_RTL8735B
#define WARN_IF_UNUSED __attribute__((warn_unused_result))
#define NEW_NOTHROW new (std::nothrow)
