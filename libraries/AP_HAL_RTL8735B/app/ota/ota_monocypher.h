/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * Monocypher 用 ArduPilot 樹裡那一份（libraries/AP_CheckFirmware/），不另放一份。那份標頭沒有 extern "C"、
 * ArduPilot 以 C++ 編它；這個目錄的 ota_monocypher.c 把同一份原始碼以 C 再編一次，C 的符號給接收端用。
 * 相對路徑以 libraries/AP_HAL_RTL8735B/app/ota/ 為準。
 *
 * SDK 的 component/mbed/targets/hal/rtl8735b/crypto_api.c 也定義了 crypto_poly1305、crypto_poly1305_init，
 * 同名會在連結映像時重複定義。接收端用不到 Poly1305，所以這組函式在這裡改名（ota_monocypher.c 先引入本檔，
 * 改名同時套用在宣告與定義上）；驗簽章用的 crypto_check_* 與 crypto_blake2b_* 照原名。
 */
#define crypto_poly1305        ota_monocypher_poly1305
#define crypto_poly1305_init   ota_monocypher_poly1305_init
#define crypto_poly1305_update ota_monocypher_poly1305_update
#define crypto_poly1305_final  ota_monocypher_poly1305_final

#include "../../../AP_CheckFirmware/monocypher.h"
