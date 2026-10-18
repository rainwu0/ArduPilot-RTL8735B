/* wifi_shim.c 的主機測試：以替身 SDK 驗證呼叫順序、參數轉換、錯誤傳遞與非阻塞旗標；不模擬無線電。 */

#include "wifi_shim.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "fake_wifi_sdk.h"

/* 生產 shim 只連結帳密符號，測試提供替身資料。 */
const uint8_t rtl8735b_wifi_ssid[33] = {0};
const uint8_t rtl8735b_wifi_ssid_len = 0;
const uint8_t rtl8735b_wifi_password[65] = {0};
const uint8_t rtl8735b_wifi_password_len = 0;

static char calls[512];
static int wifi_on_result;
static int connect_result;
static uint8_t dhcp_result;
static int associated_result;
static rtw_network_info_t last_connect;
static char last_password[70];
static unsigned char last_block;
static unsigned char ip_bytes[4];
static unsigned char mask_bytes[4];
static int bind_result;
static int setsockopt_result;
static int fcntl_result;
static int socket_result;
static int last_fcntl_cmd, last_fcntl_val, last_sockopt_name, last_sockopt_value;
static struct sockaddr_in last_bind;
static int recv_result;
static int recv_errno;
static struct sockaddr_in recv_from;
static int last_recv_flags;
static struct sockaddr_in last_sendto;
static int last_send_flags;
static size_t last_send_size;
static int send_result;
static int send_errno;
static int closed_fd;
static int autoreconnect_result;
static int powersave_result;
/* TCP：listen socket 沿用 socket_result（7），accept 成功時回傳 ACCEPTED_FD */
#define ACCEPTED_FD 9
static int last_socket_type;
static int listen_result, last_listen_backlog;
static int accept_result, accept_errno;
static struct sockaddr_in accept_from;
static int nodelay_result;
static int tcp_recv_result, tcp_recv_errno;
static int tcp_send_result, tcp_send_errno;
static size_t last_tcp_send_size;
static int last_tcp_flags;

static void record(const char *name)
{
    strncat(calls, name, sizeof(calls) - strlen(calls) - 2);
    strncat(calls, ";", sizeof(calls) - strlen(calls) - 1);
}

static void reset(void)
{
    calls[0] = '\0';
    wifi_on_result = 0;
    connect_result = RTW_SUCCESS;
    dhcp_result = DHCP_ADDRESS_ASSIGNED;
    associated_result = RTW_ERROR;
    memset(&last_connect, 0, sizeof(last_connect));
    memset(last_password, 0, sizeof(last_password));
    last_block = 0;
    memset(ip_bytes, 0, sizeof(ip_bytes));
    memset(mask_bytes, 0, sizeof(mask_bytes));
    bind_result = 0;
    setsockopt_result = 0;
    fcntl_result = 0;
    socket_result = 7;
    recv_result = -1;
    recv_errno = EWOULDBLOCK;
    send_result = 0;
    send_errno = EHOSTUNREACH;
    closed_fd = -1;
    autoreconnect_result = 0;
    powersave_result = RTW_SUCCESS;
    last_socket_type = -1;
    listen_result = 0;
    last_listen_backlog = -1;
    accept_result = ACCEPTED_FD;
    accept_errno = EWOULDBLOCK;
    memset(&accept_from, 0, sizeof(accept_from));
    nodelay_result = 0;
    tcp_recv_result = -1;
    tcp_recv_errno = EWOULDBLOCK;
    tcp_send_result = 0;
    tcp_send_errno = EWOULDBLOCK;
    last_tcp_send_size = 0;
    last_tcp_flags = 0;
}

/* SDK 替身 */
void LwIP_Init(void) { record("LwIP_Init"); }
int wifi_on(rtw_mode_t mode) { assert(mode == RTW_MODE_STA); record("wifi_on"); return wifi_on_result; }
int wifi_config_autoreconnect(uint8_t mode, uint8_t retry, uint16_t timeout)
{
    assert(mode == 0 && retry == 0 && timeout == 0);
    record("autoreconnect_off");
    return autoreconnect_result;
}
int wifi_set_powersave_mode(uint8_t ips, uint8_t lps)
{
    assert(ips == IPS_MODE_NONE && lps == LPS_MODE_NONE);
    record("powersave_off");
    return powersave_result;
}
int wifi_connect(rtw_network_info_t *p, unsigned char block)
{
    record("wifi_connect");
    last_connect = *p;
    last_block = block;
    if (p->password != NULL && p->password_len > 0) {
        memcpy(last_password, p->password, (size_t)p->password_len);
    }
    return connect_result;
}
int wifi_disconnect(void) { record("wifi_disconnect"); return 0; }
int wifi_is_connected_to_ap(void) { return associated_result; }
uint8_t LwIP_DHCP(uint8_t idx, uint8_t state)
{
    assert(idx == 0 && state == DHCP_START);
    record("dhcp");
    return dhcp_result;
}
unsigned char *LwIP_GetIP(uint8_t idx) { assert(idx == 0); return ip_bytes; }
uint8_t *LwIP_GetMASK(uint8_t idx) { assert(idx == 0); return mask_bytes; }

uint16_t lwip_htons(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
uint32_t lwip_htonl(uint32_t v)
{
    return ((v & 0xffu) << 24) | ((v & 0xff00u) << 8) | ((v >> 8) & 0xff00u) | (v >> 24);
}
int lwip_socket(int domain, int type, int protocol)
{
    assert(domain == AF_INET);
    assert((type == SOCK_DGRAM && protocol == IPPROTO_UDP) || (type == SOCK_STREAM && protocol == IPPROTO_TCP));
    last_socket_type = type;
    record("socket");
    return socket_result;
}
int lwip_setsockopt(int s, int level, int name, const void *value, socklen_t len)
{
    assert(len == sizeof(int));
    if (level == IPPROTO_TCP) {
        assert(s == ACCEPTED_FD && name == TCP_NODELAY && *(const int *)value == 1);
        record("tcp_nodelay");
        return nodelay_result;
    }
    assert(s == socket_result && level == SOL_SOCKET);
    last_sockopt_name = name;
    last_sockopt_value = *(const int *)value;
    record("setsockopt");
    return setsockopt_result;
}
int lwip_bind(int s, const struct sockaddr *name, socklen_t len)
{
    assert(s == socket_result && len == sizeof(struct sockaddr_in));
    memcpy(&last_bind, name, sizeof(last_bind));
    record("bind");
    return bind_result;
}
int lwip_fcntl(int s, int cmd, int val)
{
    assert(s == socket_result || s == ACCEPTED_FD);
    last_fcntl_cmd = cmd;
    last_fcntl_val = val;
    record("fcntl");
    return fcntl_result;
}
int lwip_close(int s) { closed_fd = s; record("close"); return 0; }
ssize_t lwip_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    assert(s == 7 && *fromlen == sizeof(struct sockaddr_in));
    last_recv_flags = flags;
    if (recv_result < 0) {
        errno = recv_errno;
        return -1;
    }
    memset(mem, 0xab, (size_t)recv_result < len ? (size_t)recv_result : len);
    memcpy(from, &recv_from, sizeof(recv_from));
    return recv_result;
}
ssize_t lwip_sendto(int s, const void *data, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
    assert(s == 7 && data != NULL && tolen == sizeof(struct sockaddr_in));
    memcpy(&last_sendto, to, sizeof(last_sendto));
    last_send_flags = flags;
    last_send_size = size;
    if (send_result < 0) {
        errno = send_errno;
        return -1;
    }
    return (ssize_t)size;
}
int lwip_listen(int s, int backlog)
{
    assert(s == socket_result);
    last_listen_backlog = backlog;
    record("listen");
    return listen_result;
}
int lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    assert(s == socket_result && *addrlen == sizeof(struct sockaddr_in));
    record("accept");
    if (accept_result < 0) {
        errno = accept_errno;
        return -1;
    }
    memcpy(addr, &accept_from, sizeof(accept_from));
    return accept_result;
}
ssize_t lwip_recv(int s, void *mem, size_t len, int flags)
{
    assert(s == ACCEPTED_FD);
    last_tcp_flags = flags;
    if (tcp_recv_result < 0) {
        errno = tcp_recv_errno;
        return -1;
    }
    memset(mem, 0xcd, (size_t)tcp_recv_result < len ? (size_t)tcp_recv_result : len);
    return tcp_recv_result;
}
ssize_t lwip_send(int s, const void *data, size_t size, int flags)
{
    assert(s == ACCEPTED_FD && data != NULL);
    last_tcp_flags = flags;
    last_tcp_send_size = size;
    if (tcp_send_result < 0) {
        errno = tcp_send_errno;
        return -1;
    }
    return tcp_send_result;
}

static void test_start_initialises_lwip_once_and_disables_sdk_policies(void)
{
    reset();
    wifi_on_result = -1;
    assert(rtl8735b_wifi_start_sta() == RTL8735B_WIFI_ERR_START);
    assert(strcmp(calls, "LwIP_Init;wifi_on;") == 0);

    reset();
    assert(rtl8735b_wifi_start_sta() == RTL8735B_WIFI_OK);
    /* LwIP_Init 建立 tcpip thread，不能做第二次；自動重連與省電由本 HAL 控制。 */
    assert(strcmp(calls, "wifi_on;autoreconnect_off;powersave_off;") == 0);

    reset();
    wifi_on_result = 1;   /* SDK：已在執行時回傳 1 */
    assert(rtl8735b_wifi_start_sta() == RTL8735B_WIFI_OK);

    /* 關不掉 SDK 自動重連或省電時回報失敗，不假裝鏈路政策已生效 */
    reset();
    autoreconnect_result = -1;
    assert(rtl8735b_wifi_start_sta() == RTL8735B_WIFI_ERR_START);
    assert(strcmp(calls, "wifi_on;autoreconnect_off;") == 0);
    reset();
    powersave_result = RTW_ERROR;
    assert(rtl8735b_wifi_start_sta() == RTL8735B_WIFI_ERR_START);
}

static void test_join_wpa2_and_open(void)
{
    static const uint8_t ssid[] = "lab-net";
    static const uint8_t password[] = "12345678";
    reset();
    assert(rtl8735b_wifi_join(ssid, 7, password, 8) == RTL8735B_WIFI_OK);
    assert(strcmp(calls, "wifi_connect;dhcp;") == 0);
    assert(last_block == 1);
    assert(last_connect.ssid.len == 7 && memcmp(last_connect.ssid.val, "lab-net", 8) == 0);
    assert(last_connect.security_type == RTW_SECURITY_WPA2_AES_PSK);
    assert(last_connect.password_len == 8 && strcmp(last_password, "12345678") == 0);
    assert(last_connect.channel == 0 && last_connect.joinstatus_user_callback == NULL);

    reset();
    assert(rtl8735b_wifi_join(ssid, 7, NULL, 0) == RTL8735B_WIFI_OK);
    assert(last_connect.security_type == RTW_SECURITY_OPEN && last_connect.password_len == 0);
}

static void test_join_failures_are_reported(void)
{
    static const uint8_t ssid[] = "n";
    static const uint8_t password[65] = "1234567";
    reset();
    assert(rtl8735b_wifi_join(ssid, 0, NULL, 0) == RTL8735B_WIFI_ERR_ARG);
    assert(rtl8735b_wifi_join(ssid, 33, NULL, 0) == RTL8735B_WIFI_ERR_ARG);
    assert(rtl8735b_wifi_join(ssid, 1, password, 7) == RTL8735B_WIFI_ERR_ARG);
    assert(rtl8735b_wifi_join(ssid, 1, password, 65) == RTL8735B_WIFI_ERR_ARG);
    assert(calls[0] == '\0');

    reset();
    connect_result = RTW_TIMEOUT;
    assert(rtl8735b_wifi_join(ssid, 1, NULL, 0) == RTL8735B_WIFI_ERR_JOIN);
    assert(strcmp(calls, "wifi_connect;") == 0);

    /* DHCP 沒有回報取得位址（例如逾時，SDK 這時可能改設靜態位址）時，不能當成已連線。 */
    reset();
    dhcp_result = DHCP_ADDRESS_ASSIGNED + 1;
    assert(rtl8735b_wifi_join(ssid, 1, NULL, 0) == RTL8735B_WIFI_ERR_DHCP);
    assert(strcmp(calls, "wifi_connect;dhcp;wifi_disconnect;") == 0);

    reset();
    dhcp_result = 0;
    assert(rtl8735b_wifi_join(ssid, 1, NULL, 0) == RTL8735B_WIFI_ERR_DHCP);
}

static void test_association_and_address(void)
{
    uint32_t address = 1, netmask = 1;
    reset();
    assert(rtl8735b_wifi_is_associated() == 0);
    associated_result = RTW_SUCCESS;
    assert(rtl8735b_wifi_is_associated() == 1);

    assert(rtl8735b_wifi_get_ipv4(&address, &netmask) == RTL8735B_WIFI_ERR_DHCP);
    ip_bytes[0] = 192; ip_bytes[1] = 168; ip_bytes[2] = 1; ip_bytes[3] = 23;
    mask_bytes[0] = 255; mask_bytes[1] = 255; mask_bytes[2] = 255; mask_bytes[3] = 0;
    assert(rtl8735b_wifi_get_ipv4(&address, &netmask) == RTL8735B_WIFI_OK);
    assert(address == 0xc0a80117u && netmask == 0xffffff00u);

    rtl8735b_wifi_leave();
    assert(strcmp(calls, "wifi_disconnect;") == 0);
}

static void test_udp_socket_lifecycle(void)
{
    reset();
    assert(rtl8735b_udp_open(14555) == 7);
    assert(strcmp(calls, "socket;setsockopt;bind;fcntl;") == 0);
    assert(last_sockopt_name == SO_BROADCAST && last_sockopt_value == 1);
    assert(last_bind.sin_family == AF_INET && last_bind.sin_addr.s_addr == 0);
    assert(last_bind.sin_port == lwip_htons(14555));
    assert(last_fcntl_cmd == F_SETFL && last_fcntl_val == O_NONBLOCK);

    reset();
    socket_result = -1;
    assert(rtl8735b_udp_open(14555) == RTL8735B_WIFI_ERR_SOCKET);

    reset();
    bind_result = -1;
    assert(rtl8735b_udp_open(14555) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == 7);

    reset();
    fcntl_result = -1;
    assert(rtl8735b_udp_open(14555) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == 7);

    reset();
    setsockopt_result = -1;
    assert(rtl8735b_udp_open(14555) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == 7);

    rtl8735b_udp_close(-1);   /* 無效 fd 不呼叫 SDK */
    closed_fd = -1;
    rtl8735b_udp_close(7);
    assert(closed_fd == 7);
}

static void test_udp_io(void)
{
    uint8_t buffer[16];
    uint32_t address = 0;
    uint16_t port = 0;
    reset();
    /* 沒有資料：lwIP 以 -1 加 EWOULDBLOCK 表示，shim 回傳 0 */
    assert(rtl8735b_udp_recvfrom(7, buffer, sizeof(buffer), &address, &port) == 0);
    assert(last_recv_flags & MSG_DONTWAIT);
    recv_errno = EBADF;
    assert(rtl8735b_udp_recvfrom(7, buffer, sizeof(buffer), &address, &port) < 0);

    recv_result = 5;
    recv_from.sin_family = AF_INET;
    recv_from.sin_port = lwip_htons(14550);
    recv_from.sin_addr.s_addr = lwip_htonl(0xc0a80164u);
    assert(rtl8735b_udp_recvfrom(7, buffer, sizeof(buffer), &address, &port) == 5);
    assert(address == 0xc0a80164u && port == 14550 && buffer[0] == 0xab);

    assert(rtl8735b_udp_sendto(7, buffer, 9, 0xc0a801ffu, 14550) == 9);
    assert(last_send_size == 9 && (last_send_flags & MSG_DONTWAIT));
    assert(last_sendto.sin_family == AF_INET);
    assert(last_sendto.sin_addr.s_addr == lwip_htonl(0xc0a801ffu));
    assert(last_sendto.sin_port == lwip_htons(14550));
    send_result = -1;
    assert(rtl8735b_udp_sendto(7, buffer, 9, 0xc0a801ffu, 14550) == RTL8735B_WIFI_ERR_SOCKET);
    /* 驅動要不到封包緩衝（lwIP 的 ERR_BUF、ERR_MEM）：回緩衝不足，由 WiFiDriver 留著重試 */
    send_errno = ENOBUFS;
    assert(rtl8735b_udp_sendto(7, buffer, 9, 0xc0a801ffu, 14550) == RTL8735B_WIFI_ERR_NOBUFS);
    send_errno = ENOMEM;
    assert(rtl8735b_udp_sendto(7, buffer, 9, 0xc0a801ffu, 14550) == RTL8735B_WIFI_ERR_NOBUFS);
    assert(rtl8735b_udp_sendto(-1, buffer, 9, 0xc0a801ffu, 14550) == RTL8735B_WIFI_ERR_ARG);
}

static void test_tcp_listen(void)
{
    reset();
    assert(rtl8735b_tcp_listen(5760) == 7);
    assert(strcmp(calls, "socket;setsockopt;bind;listen;fcntl;") == 0);
    assert(last_socket_type == SOCK_STREAM);
    assert(last_sockopt_name == SO_REUSEADDR && last_sockopt_value == 1);
    assert(last_bind.sin_family == AF_INET && last_bind.sin_addr.s_addr == 0);
    assert(last_bind.sin_port == lwip_htons(5760));
    assert(last_listen_backlog == 1);
    assert(last_fcntl_cmd == F_SETFL && last_fcntl_val == O_NONBLOCK);

    /* 組態沒有 SO_REUSE 時 SO_REUSEADDR 會失敗，仍照常監聽 */
    reset();
    setsockopt_result = -1;
    assert(rtl8735b_tcp_listen(5760) == 7);

    reset();
    socket_result = -1;
    assert(rtl8735b_tcp_listen(5760) == RTL8735B_WIFI_ERR_SOCKET);

    reset();
    bind_result = -1;
    assert(rtl8735b_tcp_listen(5760) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == 7);

    reset();
    listen_result = -1;
    assert(rtl8735b_tcp_listen(5760) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == 7);

    reset();
    fcntl_result = -1;
    assert(rtl8735b_tcp_listen(5760) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == 7);
}

static void test_tcp_accept(void)
{
    uint32_t address = 0;
    uint16_t port = 0;

    /* 沒有等待中的連線不是錯誤 */
    reset();
    accept_result = -1;
    assert(rtl8735b_tcp_accept(7, &address, &port) == RTL8735B_WIFI_NONE);
    accept_errno = EAGAIN;
    assert(rtl8735b_tcp_accept(7, &address, &port) == RTL8735B_WIFI_NONE);
    accept_errno = ECONNABORTED;
    assert(rtl8735b_tcp_accept(7, &address, &port) == RTL8735B_WIFI_ERR_SOCKET);

    /* 接受的連線設為非阻塞並關閉 Nagle，位址轉成主機位元組序 */
    reset();
    accept_from.sin_family = AF_INET;
    accept_from.sin_port = lwip_htons(50000);
    accept_from.sin_addr.s_addr = lwip_htonl(0xc0a80164u);
    assert(rtl8735b_tcp_accept(7, &address, &port) == ACCEPTED_FD);
    assert(strcmp(calls, "accept;fcntl;tcp_nodelay;") == 0);
    assert(last_fcntl_cmd == F_SETFL && last_fcntl_val == O_NONBLOCK);
    assert(address == 0xc0a80164u && port == 50000);

    /* 設定連線失敗時關閉該連線，不留下半設定的 fd */
    reset();
    nodelay_result = -1;
    assert(rtl8735b_tcp_accept(7, &address, &port) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == ACCEPTED_FD);

    reset();
    fcntl_result = -1;
    assert(rtl8735b_tcp_accept(7, &address, &port) == RTL8735B_WIFI_ERR_SOCKET);
    assert(closed_fd == ACCEPTED_FD);

    assert(rtl8735b_tcp_accept(-1, &address, &port) == RTL8735B_WIFI_ERR_ARG);
}

static void test_tcp_io(void)
{
    uint8_t buffer[16];
    reset();

    /* 接收：沒有資料回 0；對端關閉回 ERR_CLOSED；其他錯誤 < 0 */
    assert(rtl8735b_tcp_recv(ACCEPTED_FD, buffer, sizeof(buffer)) == 0);
    assert(last_tcp_flags & MSG_DONTWAIT);
    tcp_recv_result = 0;
    assert(rtl8735b_tcp_recv(ACCEPTED_FD, buffer, sizeof(buffer)) == RTL8735B_WIFI_ERR_CLOSED);
    tcp_recv_result = -1;
    tcp_recv_errno = ECONNRESET;
    assert(rtl8735b_tcp_recv(ACCEPTED_FD, buffer, sizeof(buffer)) == RTL8735B_WIFI_ERR_SOCKET);
    tcp_recv_result = 5;
    assert(rtl8735b_tcp_recv(ACCEPTED_FD, buffer, sizeof(buffer)) == 5 && buffer[0] == 0xcd);
    assert(rtl8735b_tcp_recv(-1, buffer, sizeof(buffer)) == RTL8735B_WIFI_ERR_ARG);

    /* 送出：部分送出照實回報；送出緩衝區滿回 0；其他錯誤 < 0 */
    tcp_send_result = 4;
    assert(rtl8735b_tcp_send(ACCEPTED_FD, buffer, 9) == 4);
    assert(last_tcp_send_size == 9 && (last_tcp_flags & MSG_DONTWAIT));
    tcp_send_result = -1;
    tcp_send_errno = EWOULDBLOCK;
    assert(rtl8735b_tcp_send(ACCEPTED_FD, buffer, 9) == 0);
    tcp_send_errno = ECONNRESET;
    assert(rtl8735b_tcp_send(ACCEPTED_FD, buffer, 9) == RTL8735B_WIFI_ERR_SOCKET);
    assert(rtl8735b_tcp_send(-1, buffer, 9) == RTL8735B_WIFI_ERR_ARG);

    rtl8735b_tcp_close(-1);   /* 無效 fd 不呼叫 SDK */
    closed_fd = -1;
    rtl8735b_tcp_close(ACCEPTED_FD);
    assert(closed_fd == ACCEPTED_FD);
}

int main(void)
{
    test_start_initialises_lwip_once_and_disables_sdk_policies();
    test_join_wpa2_and_open();
    test_join_failures_are_reported();
    test_association_and_address();
    test_udp_socket_lifecycle();
    test_udp_io();
    test_tcp_listen();
    test_tcp_accept();
    test_tcp_io();
    printf("wifi_shim host test: PASS\n");
    return 0;
}
