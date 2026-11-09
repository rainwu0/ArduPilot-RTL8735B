/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "StatusText.h"

#include <stdarg.h>
#include <stdio.h>

#include <GCS_MAVLink/GCS.h>

static void report(MAV_SEVERITY severity, const char *fmt, va_list ap)
{
    char text[200];
    vsnprintf(text, sizeof(text), fmt, ap);
    ::printf("%s\n", text);
#if AP_HAVE_GCS_SEND_TEXT
    // GCS::send_textv 只把文字排進佇列（太長時分段），由主迴圈送出，任何 task 都可以呼叫
    GCS_SEND_TEXT(severity, "%s", text);
#else
    (void)severity;
#endif
}

void RTL8735B::report_warning(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(MAV_SEVERITY_WARNING, fmt, ap);
    va_end(ap);
}

void RTL8735B::report_info(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    report(MAV_SEVERITY_INFO, fmt, ap);
    va_end(ap);
}
