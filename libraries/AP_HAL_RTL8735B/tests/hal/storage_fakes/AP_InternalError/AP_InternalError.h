#pragma once
// 主機測試：真實的 INTERNAL_ERROR 只記錄錯誤、程式繼續執行。這裡計數；
// bitmask_range 代表本 HAL 的索引錯誤，直接讓測試失敗。
#include <stdio.h>
#include <stdexcept>
namespace AP_InternalError {
enum class error_t { bitmask_range, switch_full_sector_recursion };
inline unsigned recursion_errors = 0;
inline void report(error_t e, const char *file, int line)
{
    if (e == error_t::bitmask_range) {
        fprintf(stderr, "INTERNAL_ERROR bitmask_range at %s:%d\n", file, line);
        throw std::logic_error("internal error");
    }
    recursion_errors++;
}
}
#define INTERNAL_ERROR(error_number) AP_InternalError::report(error_number, __FILE__, __LINE__)
