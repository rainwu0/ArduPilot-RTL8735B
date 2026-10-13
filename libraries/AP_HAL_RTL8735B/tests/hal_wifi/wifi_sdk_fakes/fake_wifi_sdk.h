#pragma once

/* wifi_shim.c 的測試替身 SDK：只宣告 shim 用到的名稱，自己撰寫。出處：
 * - Wi-Fi 的型別與常數：SDK d1b6426 的 component/wifi/driver/include/wifi_constants.h、
 *   wifi_structures.h（Apache-2.0）；
 * - socket 的名稱與常數：SDK d1b6426 內附 lwIP 2.1.2 的 src/include/lwip/sockets.h（lwIP 的 BSD 授權）；
 * - DHCP 狀態與 LwIP_* 函式：只寫 shim 用到的名稱，數值由本測試自訂。
 * 實際 SDK 型別與巨集另由交叉建置核對。 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/* wifi_constants.h */
typedef enum { RTW_SUCCESS = 0, RTW_ERROR = -1, RTW_TIMEOUT = 2, RTW_INVALID_KEY = 4, RTW_BUSY = -16 } rtw_result_t;
typedef enum { RTW_MODE_NONE = 0, RTW_MODE_STA, RTW_MODE_AP } rtw_mode_t;
typedef enum { RTW_SECURITY_OPEN = 0, RTW_SECURITY_WPA2_AES_PSK = 0x00400004 } rtw_security_t;
enum { IPS_MODE_NONE = 0, LPS_MODE_NONE = 0 };

/* wifi_structures.h */
typedef struct { unsigned char len; unsigned char val[33]; } rtw_ssid_t;
typedef struct { unsigned char octet[6]; } rtw_mac_t;
typedef struct {
    rtw_ssid_t ssid;
    rtw_mac_t bssid;
    rtw_security_t security_type;
    unsigned char *password;
    int password_len;
    int key_id;
    unsigned char channel;
    unsigned char pscan_option;
    void *joinstatus_user_callback;
} rtw_network_info_t;

int wifi_on(rtw_mode_t mode);
int wifi_connect(rtw_network_info_t *connect_param, unsigned char block);
int wifi_disconnect(void);
int wifi_is_connected_to_ap(void);
int wifi_set_powersave_mode(uint8_t ips_mode, uint8_t lps_mode);
int wifi_config_autoreconnect(uint8_t mode, uint8_t retry_times, uint16_t timeout);

/* DHCP：數值由本測試自訂 */
enum { DHCP_START = 1, DHCP_ADDRESS_ASSIGNED = 2 };
void LwIP_Init(void);
uint8_t LwIP_DHCP(uint8_t idx, uint8_t dhcp_state);
unsigned char *LwIP_GetIP(uint8_t idx);
uint8_t *LwIP_GetMASK(uint8_t idx);

/* lwip/sockets.h（lwIP 2.1.2 的名稱與預設值） */
#define AF_INET 2
#define SOCK_DGRAM 2
#define IPPROTO_UDP 17
#define SOL_SOCKET 0xfff
#define SO_BROADCAST 0x0020
#define F_GETFL 3
#define F_SETFL 4
#define O_NONBLOCK 1
#define MSG_DONTWAIT 0x08
typedef uint32_t socklen_t;
typedef uint8_t sa_family_t;
typedef uint16_t in_port_t;
struct in_addr { uint32_t s_addr; };
struct sockaddr { uint8_t sa_len; sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_in {
    uint8_t sin_len;
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};
uint16_t lwip_htons(uint16_t v);
uint32_t lwip_htonl(uint32_t v);
#define htons(x) lwip_htons(x)
#define ntohs(x) lwip_htons(x)
#define htonl(x) lwip_htonl(x)
#define ntohl(x) lwip_htonl(x)
int lwip_socket(int domain, int type, int protocol);
int lwip_bind(int s, const struct sockaddr *name, socklen_t namelen);
int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen);
int lwip_fcntl(int s, int cmd, int val);
int lwip_close(int s);
ssize_t lwip_recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen);
ssize_t lwip_sendto(int s, const void *dataptr, size_t size, int flags, const struct sockaddr *to, socklen_t tolen);

/* TCP（lwIP 2.1.2 src/include/lwip/sockets.h:190、197、252、281、579–596） */
#define SOCK_STREAM 1
#define SO_REUSEADDR 0x0004
#define IPPROTO_TCP 6
#define TCP_NODELAY 0x01
int lwip_listen(int s, int backlog);
int lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen);
ssize_t lwip_recv(int s, void *mem, size_t len, int flags);
ssize_t lwip_send(int s, const void *dataptr, size_t size, int flags);
