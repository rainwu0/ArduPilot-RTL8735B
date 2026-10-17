/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

/*
 * HAL 執行期的警告：照舊 printf 到主控台，同時以 STATUSTEXT 送給地面站，比照 AP_HAL_ChibiOS
 * 在 HAL 內呼叫 GCS_SEND_TEXT（ArduPilot 9f648ccabc 的 libraries/AP_HAL_ChibiOS/GPIO.cpp）。
 * SERIAL0 跑 MAVLink 時，主控台的文字夾在封包之間，地面站從 STATUSTEXT 讀得到同樣的訊息。
 * 實作在 StatusText.cpp，呼叫端不必引入 GCS_MAVLink。fmt 不含結尾換行。
 */

namespace RTL8735B {

__attribute__((format(printf, 1, 2)))
void report_warning(const char *fmt, ...);

}
