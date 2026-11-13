/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 把 ArduPilot 的 Monocypher（libraries/AP_CheckFirmware/monocypher.cpp）以 C 編進應用程式，見 ota_monocypher.h。
 * 建置只收應用程式目錄最上層的 .c，所以由這個檔引入。先引入 ota_monocypher.h，和 SDK 同名的函式才會改名。
 */

#include "ota_monocypher.h"
#include "../../../AP_CheckFirmware/monocypher.cpp"
