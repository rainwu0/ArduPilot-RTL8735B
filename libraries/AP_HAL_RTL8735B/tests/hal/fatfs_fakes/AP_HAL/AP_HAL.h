#pragma once
// 測試替身。名稱照 ArduPilot 的 AP_HAL（GPL-3.0-or-later），只宣告受測程式用到的部分，行為由
// sdcard_fatfs_host_test.cpp 定義：時間、執行緒、解鎖狀態都由測試控制。
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <functional>
#include <mutex>
#include <new>
#include "AP_HAL_Boards.h"

#define NEW_NOTHROW new (std::nothrow)
#define EXPECT_DELAY_MS(ms) do { } while (0)
#define FUNCTOR_BIND_MEMBER(func, rettype, ...) std::function<rettype(__VA_ARGS__)>(std::bind(func, this))

typedef std::recursive_mutex HAL_Semaphore;
#define WITH_SEMAPHORE(sem) std::lock_guard<std::recursive_mutex> _sem_guard(sem)

size_t strncpy_noterm(char *dest, const char *src, size_t n);

namespace AP_HAL {
uint32_t millis();
typedef std::function<void(void)> Proc;

class Scheduler {
public:
    void delay(uint16_t ms);
    bool in_main_thread() const;
    void register_io_process(Proc proc);
};

class Util {
public:
    enum Memory_Type { MEM_DMA_SAFE, MEM_FAST };
    bool get_soft_armed() const;
    void *malloc_type(size_t size, Memory_Type mem_type);
    void free_type(void *ptr, size_t size, Memory_Type mem_type);
};

class HAL {
public:
    Scheduler *scheduler;
    Util *util;
};
}  // namespace AP_HAL
