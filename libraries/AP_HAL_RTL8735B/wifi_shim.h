/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

/*
 * Wi-Fi STA 與 lwIP UDP socket 的 C 包裝。SDK 的 wifi_conf.h、lwip 標頭在 C++ 下有型別衝突，
 * 且 lwip/sockets.h 會把 close、read、write 定義成巨集，所以 SDK 呼叫都留在以 C 編譯的 wifi_shim.c。
 * 位址一律用主機位元組序的 IPv4（a.b.c.d 為 (a << 24) | (b << 16) | (c << 8) | d）。
 *
 * 阻塞特性：rtl8735b_wifi_start_sta() 與 rtl8735b_wifi_join() 會阻塞數秒到數十秒
 * （SDK 同步連線與 DHCP），只能在 Wi-Fi 專用 task 呼叫；其餘函式不等待網路。
 */

#include <stdint.h>

enum {
    RTL8735B_WIFI_OK = 0,
    RTL8735B_WIFI_ERR_ARG = -1,
    RTL8735B_WIFI_ERR_START = -2,
    RTL8735B_WIFI_ERR_JOIN = -3,
    RTL8735B_WIFI_ERR_DHCP = -4,
    RTL8735B_WIFI_ERR_SOCKET = -5,
};

#ifdef __cplusplus
extern "C" {
#endif

// 建置時由本機設定檔產生（hwdef/scripts/rtl8735b_wifi_config.py）；ssid_len 為 0 表示未設定。
extern const uint8_t rtl8735b_wifi_ssid[33];
extern const uint8_t rtl8735b_wifi_ssid_len;
extern const uint8_t rtl8735b_wifi_password[65];
extern const uint8_t rtl8735b_wifi_password_len;

// 初始化 lwIP（只做一次）並以 STA 模式開啟 Wi-Fi；關閉 SDK 自動重連與省電。阻塞。
int rtl8735b_wifi_start_sta(void);

// 同步連線到 AP，再以 DHCP 取得位址。password_len 為 0 時連開放網路，否則 WPA2-PSK（AES）。
// DHCP 逾時（SDK 會改設靜態位址）視為失敗並斷線。阻塞。
int rtl8735b_wifi_join(const uint8_t *ssid, uint8_t ssid_len, const uint8_t *password, uint8_t password_len);

// 1：與 AP 保持連線；0：未連線。不等待。
int rtl8735b_wifi_is_associated(void);

// 讀取介面 0 的位址與網路遮罩；位址為 0 時回傳 RTL8735B_WIFI_ERR_DHCP。
int rtl8735b_wifi_get_ipv4(uint32_t *address, uint32_t *netmask);

// 斷開目前連線（不等待）。
void rtl8735b_wifi_leave(void);

// 建立非阻塞、允許廣播、綁定 local_port 的 UDP socket；回傳 fd（>= 0）或錯誤碼。
int rtl8735b_udp_open(uint16_t local_port);

// 非阻塞接收一個資料包：> 0 為位元組數，0 為沒有資料，< 0 為錯誤。
int rtl8735b_udp_recvfrom(int fd, uint8_t *buffer, uint16_t length, uint32_t *address, uint16_t *port);

// 非阻塞送出一個資料包：回傳送出的位元組數或 < 0。
int rtl8735b_udp_sendto(int fd, const uint8_t *buffer, uint16_t length, uint32_t address, uint16_t port);

void rtl8735b_udp_close(int fd);

#ifdef __cplusplus
}
#endif
