/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * OTA 接收端的 TCP：SDK 的 lwIP socket 的非阻塞包裝，在 HAL 已建立的 Wi-Fi 連線上 listen 自己的埠，
 * 不設定 Wi-Fi。應用程式不能呼叫 HAL 內部的 rtl8735b_tcp_*（只能用 SDK 與 rtl8735b_app.h），
 * 所以照 HAL 的 wifi_shim.c 的做法在這裡另寫一份。位址一律是主機位元組序的 IPv4。
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OTA_NET_ERR_ARG = -1,
    OTA_NET_ERR_SOCKET = -5,
    OTA_NET_NONE = -6,          // 沒有等待中的連線（非錯誤）
    OTA_NET_ERR_CLOSED = -7,    // 對端已關閉 TCP 連線
};

// 建立非阻塞、綁定 port、只排一個等待連線的 listen socket；回傳 fd（>= 0）或錯誤碼。
int ota_tcp_listen(uint16_t port);

// 非阻塞接受一個連線：回傳非阻塞、關閉 Nagle 的連線 fd（>= 0）；沒有等待中的連線時為 OTA_NET_NONE，其他錯誤為 < 0。
int ota_tcp_accept(int listen_fd, uint32_t *address, uint16_t *port);

// 非阻塞接收：> 0 為位元組數，0 為沒有資料，OTA_NET_ERR_CLOSED 為對端已關閉，其他 < 0 為錯誤。
int ota_tcp_recv(int fd, uint8_t *buffer, uint16_t length);

// 非阻塞送出：回傳送出的位元組數（送出緩衝區滿時為 0）或 < 0。
int ota_tcp_send(int fd, const uint8_t *buffer, uint16_t length);

void ota_tcp_close(int fd);

#ifdef __cplusplus
}
#endif
