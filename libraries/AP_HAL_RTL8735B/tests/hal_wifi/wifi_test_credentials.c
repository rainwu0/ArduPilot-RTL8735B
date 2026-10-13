/* WiFiDriver 主機測試的帳密替身；WIFI_TEST_SSID_LEN 為 0 時模擬未設定。 */

#include "wifi_shim.h"

#ifndef WIFI_TEST_SSID_LEN
#define WIFI_TEST_SSID_LEN 7
#endif

const uint8_t rtl8735b_wifi_ssid[33] = {'l', 'a', 'b', '-', 'n', 'e', 't'};
const uint8_t rtl8735b_wifi_ssid_len = WIFI_TEST_SSID_LEN;
const uint8_t rtl8735b_wifi_password[65] = {'1', '2', '3', '4', '5', '6', '7', '8'};
const uint8_t rtl8735b_wifi_password_len = 8;
