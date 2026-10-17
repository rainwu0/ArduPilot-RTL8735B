#pragma once
// 測試替身。出處：AP_HAVE_GCS_SEND_TEXT、GCS_SEND_TEXT 的名稱與用法依 ArduPilot 9f648ccabc 的
// libraries/GCS_MAVLink/GCS.h；MAV_SEVERITY_WARNING 的值依 MAVLink common.xml（4）。只寫受測程式用到的部分，自己撰寫。

/*
 * HAL 的主機測試用：GCS_SEND_TEXT 不送出，改把「[嚴重度] 文字」一行記在 fake_statustext()。
 */

#include <stdarg.h>
#include <stdio.h>
#include <string>

#define AP_HAVE_GCS_SEND_TEXT 1

enum MAV_SEVERITY {
    MAV_SEVERITY_WARNING = 4,
};

inline std::string &fake_statustext()
{
    static std::string text;
    return text;
}

__attribute__((format(printf, 2, 3)))
inline void fake_gcs_send_text(MAV_SEVERITY severity, const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fake_statustext() += "[" + std::to_string((int)severity) + "] " + line + "\n";
}

#define GCS_SEND_TEXT(severity, format, args...) fake_gcs_send_text(severity, format, ##args)
