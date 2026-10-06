/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Wi-Fi STA 與 lwIP UDP、TCP 的 C 包裝，理由見 wifi_shim.h。連線流程依 SDK 的初始化與 AT 指令：
 * wlan_network.c 的 init_thread 先 LwIP_Init() 再 wifi_on(RTW_MODE_STA)；連線以
 * wifi_connect(&info, 1) 同步等待，再 LwIP_DHCP(0, DHCP_START)。SDK 的 wlan_network() 與
 * wifi_fast_connect_enable() 都不呼叫：前者只做上述兩步，後者會在連線成功時把連線資訊寫進 Flash。
 */

#include "wifi_shim.h"

#include <wifi_conf.h>
#include <lwip_netconf.h>
#include <lwip/sockets.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint8_t lwip_initialized;

int rtl8735b_wifi_start_sta(void)
{
    if (!lwip_initialized) {
        // LwIP_Init 會建立 tcpip thread 並登記 netif，只能做一次（SDK lwip_netconf.c）。
        LwIP_Init();
        lwip_initialized = 1;
    }
    // SDK：成功回傳 0，已在執行時回傳 1，失敗為負值。
    if (wifi_on(RTW_MODE_STA) < 0) {
        return RTL8735B_WIFI_ERR_START;
    }
    // wifi_on 在 CONFIG_AUTO_RECONNECT 時會開啟 SDK 自動重連；重連由 WiFiDriver 單一負責，
    // 避免兩個連線流程互相回傳 RTW_BUSY。關不掉就回報失敗，由呼叫端重試。
    if (wifi_config_autoreconnect(0, 0, 0) != 0) {
        printf("WiFi: cannot disable SDK auto-reconnect\n");
        return RTL8735B_WIFI_ERR_START;
    }
    // 推論：省電（LPS）會讓無線電依 beacon 週期休眠，增加延遲；飛控鏈路關閉省電。
    if (wifi_set_powersave_mode(IPS_MODE_NONE, LPS_MODE_NONE) != RTW_SUCCESS) {
        printf("WiFi: cannot disable power save\n");
        return RTL8735B_WIFI_ERR_START;
    }
    return RTL8735B_WIFI_OK;
}

int rtl8735b_wifi_join(const uint8_t *ssid, uint8_t ssid_len, const uint8_t *password, uint8_t password_len)
{
    static uint8_t password_copy[65];
    rtw_network_info_t info;

    if (ssid == NULL || ssid_len == 0 || ssid_len > 32 ||
        (password_len != 0 && (password == NULL || password_len < 8 || password_len > 64))) {
        return RTL8735B_WIFI_ERR_ARG;
    }

    memset(&info, 0, sizeof(info));
    info.ssid.len = ssid_len;
    memcpy(info.ssid.val, ssid, ssid_len);
    if (password_len == 0) {
        info.security_type = RTW_SECURITY_OPEN;
    } else {
        // SDK 的 password 參數不是 const；複製一份，不把唯讀的建置資料交給 SDK。
        memcpy(password_copy, password, password_len);
        password_copy[password_len] = 0;
        info.password = password_copy;
        info.password_len = password_len;
        info.security_type = RTW_SECURITY_WPA2_AES_PSK;
    }
    // channel 0：全頻道掃描；joinstatus_user_callback 不使用（memset 為 NULL）。

    const int join = wifi_connect(&info, 1);
    memset(password_copy, 0, sizeof(password_copy));
    if (join != RTW_SUCCESS) {
        return RTL8735B_WIFI_ERR_JOIN;
    }
    if (LwIP_DHCP(0, DHCP_START) != DHCP_ADDRESS_ASSIGNED) {
        wifi_disconnect();
        return RTL8735B_WIFI_ERR_DHCP;
    }
    return RTL8735B_WIFI_OK;
}

int rtl8735b_wifi_is_associated(void)
{
    return wifi_is_connected_to_ap() == RTW_SUCCESS ? 1 : 0;
}

static uint32_t _bytes_to_u32(const uint8_t *bytes)
{
    // LwIP_GetIP／LwIP_GetMASK 回傳 netif 內以網路位元組序存放的位址。
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

int rtl8735b_wifi_get_ipv4(uint32_t *address, uint32_t *netmask)
{
    uint8_t ip[4];
    uint8_t mask[4];
    if (address == NULL || netmask == NULL) {
        return RTL8735B_WIFI_ERR_ARG;
    }
    memcpy(ip, LwIP_GetIP(0), sizeof(ip));
    memcpy(mask, LwIP_GetMASK(0), sizeof(mask));
    *address = _bytes_to_u32(ip);
    *netmask = _bytes_to_u32(mask);
    return *address == 0 ? RTL8735B_WIFI_ERR_DHCP : RTL8735B_WIFI_OK;
}

void rtl8735b_wifi_leave(void)
{
    wifi_disconnect();
}

int rtl8735b_udp_open(uint16_t local_port)
{
    struct sockaddr_in local;
    const int enable = 1;
    const int fd = lwip_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(local_port);
    local.sin_addr.s_addr = 0;   // INADDR_ANY
    // 本 SDK 組態的 IP_SOF_BROADCAST 為 0（不過濾廣播），仍明確開啟 SO_BROADCAST，組態改變時也成立。
    if (lwip_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable)) != 0 ||
        lwip_bind(fd, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
        lwip_fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        lwip_close(fd);
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    return fd;
}

int rtl8735b_udp_recvfrom(int fd, uint8_t *buffer, uint16_t length, uint32_t *address, uint16_t *port)
{
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    if (fd < 0 || buffer == NULL || length == 0 || address == NULL || port == NULL) {
        return RTL8735B_WIFI_ERR_ARG;
    }
    memset(&from, 0, sizeof(from));
    const ssize_t n = lwip_recvfrom(fd, buffer, length, MSG_DONTWAIT, (struct sockaddr *)&from, &from_len);
    if (n < 0) {
        // SDK 的 FreeRTOS 設 configUSE_NEWLIB_REENTRANT 1，errno 是各 task 各自的。
        return (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : RTL8735B_WIFI_ERR_SOCKET;
    }
    if (from.sin_family != AF_INET) {
        return 0;
    }
    *address = ntohl(from.sin_addr.s_addr);
    *port = ntohs(from.sin_port);
    return (int)n;
}

int rtl8735b_udp_sendto(int fd, const uint8_t *buffer, uint16_t length, uint32_t address, uint16_t port)
{
    struct sockaddr_in to;
    if (fd < 0 || buffer == NULL || length == 0) {
        return RTL8735B_WIFI_ERR_ARG;
    }
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(address);
    const ssize_t n = lwip_sendto(fd, buffer, length, MSG_DONTWAIT, (const struct sockaddr *)&to, sizeof(to));
    return n < 0 ? RTL8735B_WIFI_ERR_SOCKET : (int)n;
}

void rtl8735b_udp_close(int fd)
{
    if (fd >= 0) {
        lwip_close(fd);
    }
}

static int _would_block(void)
{
    return errno == EWOULDBLOCK || errno == EAGAIN;
}

int rtl8735b_tcp_listen(uint16_t port)
{
    struct sockaddr_in local;
    const int enable = 1;
    const int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    // 重新連線後立刻重新綁定同一埠；組態沒有開 SO_REUSE 時設定會失敗，不影響第一次綁定。
    (void)lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = 0;   // INADDR_ANY
    if (lwip_bind(fd, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
        lwip_listen(fd, 1) != 0 ||
        lwip_fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        lwip_close(fd);
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    return fd;
}

int rtl8735b_tcp_accept(int listen_fd, uint32_t *address, uint16_t *port)
{
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    const int enable = 1;
    if (listen_fd < 0 || address == NULL || port == NULL) {
        return RTL8735B_WIFI_ERR_ARG;
    }
    memset(&from, 0, sizeof(from));
    const int fd = lwip_accept(listen_fd, (struct sockaddr *)&from, &from_len);
    if (fd < 0) {
        return _would_block() ? RTL8735B_WIFI_NONE : RTL8735B_WIFI_ERR_SOCKET;
    }
    // MAVLink 訊框小而頻繁，關閉 Nagle 以免延遲。
    if (lwip_fcntl(fd, F_SETFL, O_NONBLOCK) != 0 ||
        lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0) {
        lwip_close(fd);
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    *address = ntohl(from.sin_addr.s_addr);
    *port = ntohs(from.sin_port);
    return fd;
}

int rtl8735b_tcp_recv(int fd, uint8_t *buffer, uint16_t length)
{
    if (fd < 0 || buffer == NULL || length == 0) {
        return RTL8735B_WIFI_ERR_ARG;
    }
    const ssize_t n = lwip_recv(fd, buffer, length, MSG_DONTWAIT);
    if (n == 0) {
        return RTL8735B_WIFI_ERR_CLOSED;
    }
    if (n < 0) {
        return _would_block() ? 0 : RTL8735B_WIFI_ERR_SOCKET;
    }
    return (int)n;
}

int rtl8735b_tcp_send(int fd, const uint8_t *buffer, uint16_t length)
{
    if (fd < 0 || buffer == NULL || length == 0) {
        return RTL8735B_WIFI_ERR_ARG;
    }
    const ssize_t n = lwip_send(fd, buffer, length, MSG_DONTWAIT);
    if (n < 0) {
        return _would_block() ? 0 : RTL8735B_WIFI_ERR_SOCKET;
    }
    return (int)n;
}

void rtl8735b_tcp_close(int fd)
{
    if (fd >= 0) {
        lwip_close(fd);
    }
}
