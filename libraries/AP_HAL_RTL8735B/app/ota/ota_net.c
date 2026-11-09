/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * OTA 接收端的 TCP（見 ota_net.h），做法與 HAL 的 wifi_shim.c 的 TCP 部分相同。
 */

#include "ota_net.h"

#include <lwip/sockets.h>

#include <errno.h>
#include <string.h>

static int _would_block(void)
{
    return errno == EWOULDBLOCK || errno == EAGAIN;
}

int ota_tcp_listen(uint16_t port)
{
    struct sockaddr_in local;
    const int enable = 1;
    const int fd = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        return OTA_NET_ERR_SOCKET;
    }
    // Wi-Fi 重新連線後立刻重新綁定同一埠；組態沒有開 SO_REUSE 時設定會失敗，不影響第一次綁定。
    (void)lwip_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = 0;   // INADDR_ANY
    if (lwip_bind(fd, (const struct sockaddr *)&local, sizeof(local)) != 0 ||
        lwip_listen(fd, 1) != 0 ||
        lwip_fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
        lwip_close(fd);
        return OTA_NET_ERR_SOCKET;
    }
    return fd;
}

int ota_tcp_accept(int listen_fd, uint32_t *address, uint16_t *port)
{
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    const int enable = 1;
    if (listen_fd < 0 || address == NULL || port == NULL) {
        return OTA_NET_ERR_ARG;
    }
    memset(&from, 0, sizeof(from));
    const int fd = lwip_accept(listen_fd, (struct sockaddr *)&from, &from_len);
    if (fd < 0) {
        return _would_block() ? OTA_NET_NONE : OTA_NET_ERR_SOCKET;
    }
    // 協定是一來一回的小訊框，關閉 Nagle 以免每一步都多等。
    if (lwip_fcntl(fd, F_SETFL, O_NONBLOCK) != 0 ||
        lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0) {
        lwip_close(fd);
        return OTA_NET_ERR_SOCKET;
    }
    *address = ntohl(from.sin_addr.s_addr);
    *port = ntohs(from.sin_port);
    return fd;
}

int ota_tcp_recv(int fd, uint8_t *buffer, uint16_t length)
{
    if (fd < 0 || buffer == NULL || length == 0) {
        return OTA_NET_ERR_ARG;
    }
    const ssize_t n = lwip_recv(fd, buffer, length, MSG_DONTWAIT);
    if (n == 0) {
        return OTA_NET_ERR_CLOSED;
    }
    if (n < 0) {
        return _would_block() ? 0 : OTA_NET_ERR_SOCKET;
    }
    return (int)n;
}

int ota_tcp_send(int fd, const uint8_t *buffer, uint16_t length)
{
    if (fd < 0 || buffer == NULL || length == 0) {
        return OTA_NET_ERR_ARG;
    }
    const ssize_t n = lwip_send(fd, buffer, length, MSG_DONTWAIT);
    if (n < 0) {
        return _would_block() ? 0 : OTA_NET_ERR_SOCKET;
    }
    return (int)n;
}

void ota_tcp_close(int fd)
{
    if (fd >= 0) {
        lwip_close(fd);
    }
}
