/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B

#include "WiFiDriver.h"

#include "wifi_shim.h"

#include <AP_HAL/utility/packetise.h>

#include <algorithm>
#include <stdio.h>

using namespace RTL8735B;

namespace {

// MAVLink 協定常數（modules/mavlink 產生的標頭同值）；HAL 不引入整個 GCS_MAVLink。
constexpr uint8_t MAVLINK2_STX = 253;
constexpr uint8_t MAVLINK1_STX = 254;
constexpr uint32_t MAVLINK_MSG_ID_HEARTBEAT = 0;

bool is_heartbeat(const uint8_t *frame, uint16_t length)
{
    if (length >= 10 && frame[0] == MAVLINK2_STX) {
        const uint32_t msgid = frame[7] | ((uint32_t)frame[8] << 8) | ((uint32_t)frame[9] << 16);
        return msgid == MAVLINK_MSG_ID_HEARTBEAT;
    }
    if (length >= 6 && frame[0] == MAVLINK1_STX) {
        return frame[5] == MAVLINK_MSG_ID_HEARTBEAT;
    }
    return false;
}

void print_ipv4(const char *prefix, uint32_t address, uint16_t port)
{
    printf("WiFi: %s %u.%u.%u.%u:%u\n", prefix,
           (unsigned)(address >> 24), (unsigned)((address >> 16) & 0xff),
           (unsigned)((address >> 8) & 0xff), (unsigned)(address & 0xff), (unsigned)port);
}

}

WiFiDriver::WiFiDriver(Protocol protocol, uint16_t local_port, uint16_t gcs_port) :
    _protocol(protocol),
    _local_port(local_port),
    _gcs_port(gcs_port),
    _task(nullptr),
    _disabled_reported(false),
    _fd(-1),
    _listen_fd(-1),
    _sta_started(false),
    _retry_at_ms(0),
    _last_link_check_ms(0),
    _broadcast_address(0),
    _last_gcs_rx_ms(0),
    _tx_incomplete_since_ms(0),
    _tx_incomplete(false),
    _tx_generation(0)
{
    lock_write_key = 0;
    lock_read_key = 0;
    parity = 0;
    _last_options = 0;
}

bool WiFiDriver::is_initialized()
{
    return _initialized.load();
}

bool WiFiDriver::tx_pending()
{
    WITH_SEMAPHORE(_write_mutex);
    return _initialized.load() && _writebuf.available() > 0;
}

uint32_t WiFiDriver::txspace()
{
    WITH_SEMAPHORE(_write_mutex);
    return _initialized.load() ? _writebuf.space() : 0;
}

WiFiDriver::Status WiFiDriver::status() const
{
    Status s {};
    s.state = _state.load();
    s.gcs_known = _gcs_known.load();
    s.local_address = _local_address.load();
    s.gcs_address = _gcs_address.load();
    s.gcs_port = _gcs_port_learned.load();
    s.start_failures = _start_failures.load();
    s.join_attempts = _join_attempts.load();
    s.join_failures = _join_failures.load();
    s.link_losses = _link_losses.load();
    s.gcs_changes = _gcs_changes.load();
    s.rx_datagrams = _rx_datagrams.load();
    s.rx_bytes = _rx_bytes.load();
    s.rx_dropped_bytes = _rx_dropped_bytes.load();
    s.rx_foreign_datagrams = _rx_foreign_datagrams.load();
    s.tx_datagrams = _tx_datagrams.load();
    s.tx_bytes = _tx_bytes.load();
    s.tx_dropped_bytes = _tx_dropped_bytes.load();
    s.tx_errors = _tx_errors.load();
    return s;
}

void WiFiDriver::_begin(uint32_t baud, uint16_t rx_space, uint16_t tx_space)
{
    (void)baud;   // 網路埠沒有鮑率；SERIALn_BAUD 不影響本埠
    WITH_SEMAPHORE(_read_mutex);
    WITH_SEMAPHORE(_write_mutex);

    if (rtl8735b_wifi_ssid_len == 0) {
        _state.store(LinkState::DISABLED);
        if (!_disabled_reported) {
            printf("WiFi: no STA credentials in this image; WiFi MAVLink port disabled\n");
            _disabled_reported = true;
        }
        return;
    }
    if (_state.load() == LinkState::DISABLED) {
        return;   // task 建立失敗後不再重試，避免反覆配置
    }

    const uint32_t rx_size = std::max(RX_BUFFER_SIZE, (uint32_t)rx_space) + 1;
    const uint32_t tx_size = std::max(TX_BUFFER_SIZE, (uint32_t)tx_space) + 1;
    if (!_initialized.load() || _readbuf.get_size() < rx_size || _writebuf.get_size() < tx_size) {
        if (!_readbuf.set_size(rx_size) || !_writebuf.set_size(tx_size)) {
            _readbuf.set_size(0);
            _writebuf.set_size(0);
            _initialized.store(false);
            return;
        }
        _tx_generation++;
    }

    if (_task == nullptr) {
        if (xTaskCreate(_task_entry, "APM_WIFI", TASK_STACK_WORDS, this, TASK_PRIO, &_task) != pdPASS) {
            _task = nullptr;
            _readbuf.set_size(0);
            _writebuf.set_size(0);
            _initialized.store(false);
            _state.store(LinkState::DISABLED);
            printf("WiFi: failed to create APM_WIFI task\n");
            return;
        }
        _state.store(LinkState::STARTING);
    }
    _initialized.store(true);
}

void WiFiDriver::_end()
{
    // Wi-Fi 連線與 task 保留；只停止本埠的緩衝，task 會丟棄收到的資料。
    WITH_SEMAPHORE(_read_mutex);
    WITH_SEMAPHORE(_write_mutex);
    _initialized.store(false);
    _readbuf.set_size(0);
    _writebuf.set_size(0);
    _tx_generation++;
}

void WiFiDriver::_flush()
{
    // 送出由 APM_WIFI task 進行，每一步都會盡量清空 TX 緩衝區；此處不在呼叫端做網路 I/O。
}

uint32_t WiFiDriver::_available()
{
    WITH_SEMAPHORE(_read_mutex);
    return _initialized.load() ? _readbuf.available() : 0;
}

ssize_t WiFiDriver::_read(uint8_t *buffer, uint16_t count)
{
    WITH_SEMAPHORE(_read_mutex);
    if (!_initialized.load()) {
        return -1;
    }
    if (buffer == nullptr || count == 0) {
        return 0;
    }
    return _readbuf.read(buffer, count);
}

size_t WiFiDriver::_write(const uint8_t *buffer, size_t size)
{
    WITH_SEMAPHORE(_write_mutex);
    if (!_initialized.load() || buffer == nullptr || size == 0) {
        return 0;
    }
    return _writebuf.write(buffer, size);
}

bool WiFiDriver::_discard_input()
{
    WITH_SEMAPHORE(_read_mutex);
    if (!_initialized.load()) {
        return false;
    }
    _readbuf.clear();
    return true;
}

void WiFiDriver::_task_entry(void *arg)
{
    WiFiDriver *self = static_cast<WiFiDriver *>(arg);
    for (;;) {
        const uint32_t delay_ms = self->_link_step(AP_HAL::millis());
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void WiFiDriver::_set_state(LinkState state, uint32_t now_ms)
{
    _state.store(state);
    if (state == LinkState::WAIT_RETRY) {
        _retry_at_ms = now_ms + RETRY_DELAY_MS;
    }
}

void WiFiDriver::_clear_tx()
{
    WITH_SEMAPHORE(_write_mutex);
    const uint32_t stale = _writebuf.available();
    if (stale > 0) {
        _tx_dropped_bytes.fetch_add(stale);
        _writebuf.clear();
    }
    _tx_generation++;
    _tx_incomplete = false;
}

bool WiFiDriver::_open_sockets()
{
    if (_protocol == Protocol::TCP) {
        _listen_fd = rtl8735b_tcp_listen(_local_port);
        return _listen_fd >= 0;
    }
    _fd = rtl8735b_udp_open(_local_port);
    return _fd >= 0;
}

void WiFiDriver::_close_sockets()
{
    if (_protocol == Protocol::TCP) {
        rtl8735b_tcp_close(_fd);
        rtl8735b_tcp_close(_listen_fd);
    } else {
        rtl8735b_udp_close(_fd);
    }
    _fd = -1;
    _listen_fd = -1;
}

void WiFiDriver::_link_lost(uint32_t now_ms, const char *reason)
{
    _close_sockets();
    rtl8735b_wifi_leave();
    _gcs_known.store(false);
    _local_address.store(0);
    _link_losses.fetch_add(1);
    printf("WiFi: link lost (%s), reconnecting\n", reason);
    _set_state(LinkState::WAIT_RETRY, now_ms);
}

void WiFiDriver::_refresh_address()
{
    uint32_t address = 0;
    uint32_t netmask = 0;
    if (rtl8735b_wifi_get_ipv4(&address, &netmask) != RTL8735B_WIFI_OK) {
        _local_address.store(0);
        return;
    }
    _local_address.store(address);
    // /31、/32 或無效遮罩時退回有限廣播位址。
    _broadcast_address = (netmask == 0 || (~netmask) <= 1) ? 0xffffffffu : (address | ~netmask);
}

bool WiFiDriver::_gcs_active(uint32_t now_ms) const
{
    return _gcs_known.load() && (now_ms - _last_gcs_rx_ms) <= PEER_TIMEOUT_MS;
}

bool WiFiDriver::_accept_source(uint32_t address, uint16_t port, uint32_t now_ms)
{
    if (_gcs_known.load() && address == _gcs_address.load() && port == _gcs_port_learned.load()) {
        _last_gcs_rx_ms = now_ms;
        return true;
    }
    if (_gcs_active(now_ms)) {
        // 地面站活躍期間不讓其他主機接手（包含 RC override），避免同網段的其他裝置搶走鏈路。
        _rx_foreign_datagrams.fetch_add(1);
        return false;
    }
    _gcs_address.store(address);
    _gcs_port_learned.store(port);
    _gcs_known.store(true);
    _last_gcs_rx_ms = now_ms;
    _gcs_changes.fetch_add(1);
    print_ipv4("ground station", address, port);
    return true;
}

void WiFiDriver::_service_rx(uint32_t now_ms)
{
    for (uint8_t i = 0; i < MAX_DATAGRAMS_PER_STEP; i++) {
        uint32_t address = 0;
        uint16_t port = 0;
        const int n = rtl8735b_udp_recvfrom(_fd, _datagram, sizeof(_datagram), &address, &port);
        if (n == 0) {
            return;
        }
        if (n < 0) {
            return;   // 非「沒有資料」的 socket 錯誤；連線檢查會處理斷線
        }
        if (address == _local_address.load()) {
            continue;   // 自己送出的廣播
        }
        if (!_accept_source(address, port, now_ms)) {
            continue;
        }
        _rx_datagrams.fetch_add(1);
        _rx_bytes.fetch_add(n);
        WITH_SEMAPHORE(_read_mutex);
        // 放不下就整個資料包丟棄；只寫一部分會把 MAVLink 訊框切斷。
        if (!_initialized.load() || _readbuf.space() < (uint32_t)n) {
            _rx_dropped_bytes.fetch_add(n);
            continue;
        }
        _readbuf.write(_datagram, n);
    }
}

void WiFiDriver::_service_tx(uint32_t now_ms)
{
    for (uint8_t i = 0; i < MAX_DATAGRAMS_PER_STEP; i++) {
        uint16_t n = 0;
        uint32_t generation;
        {
            WITH_SEMAPHORE(_write_mutex);
            const uint32_t available = _writebuf.available();
            if (available == 0) {
                _tx_incomplete = false;
                return;
            }
            const uint16_t limit = (uint16_t)std::min<uint32_t>(available, DATAGRAM_MAX);
#if AP_MAVLINK_PACKETISE_ENABLED
            n = mavlink_packetise(_writebuf, limit);
#else
            n = limit;
#endif
            if (n == 0) {
                // 開頭的訊框還不完整：等寫入端補齊；太久沒補齊就丟一個位元組重新對齊。
                if (!_tx_incomplete) {
                    _tx_incomplete = true;
                    _tx_incomplete_since_ms = now_ms;
                } else if (now_ms - _tx_incomplete_since_ms > TX_STALL_MS) {
                    _writebuf.advance(1);
                    _tx_dropped_bytes.fetch_add(1);
                    _tx_incomplete = false;
                    continue;
                }
                return;
            }
            _tx_incomplete = false;
            _writebuf.peekbytes(_datagram, n);
            generation = _tx_generation;
        }

        bool attempted = true;
        int sent = 0;
        if (_gcs_active(now_ms)) {
            sent = rtl8735b_udp_sendto(_fd, _datagram, n, _gcs_address.load(), _gcs_port_learned.load());
        } else {
            if (_gcs_known.load()) {
                _gcs_known.store(false);
                printf("WiFi: ground station silent, broadcasting heartbeats\n");
            }
            if (is_heartbeat(_datagram, n)) {
                sent = rtl8735b_udp_sendto(_fd, _datagram, n, _broadcast_address, _gcs_port);
            } else {
                attempted = false;
                _tx_dropped_bytes.fetch_add(n);
            }
        }

        {
            // 送出期間主迴圈可能已清空或重設緩衝區；世代不同就不推進。
            WITH_SEMAPHORE(_write_mutex);
            if (generation == _tx_generation) {
                _writebuf.advance(n);
            }
        }

        if (!attempted) {
            continue;
        }
        if (sent != n) {
            // UDP 本來就可能遺失：送出失敗的訊框丟棄並計數，不在此重試，避免卡住後面的資料；
            // 這一步不再送，下一步再試後面的訊框。
            _tx_errors.fetch_add(1);
            _tx_dropped_bytes.fetch_add(n);
            return;
        }
        _tx_datagrams.fetch_add(1);
        _tx_bytes.fetch_add(n);
    }
}

uint32_t WiFiDriver::_link_step(uint32_t now_ms)
{
    switch (_state.load()) {
    case LinkState::NOT_STARTED:
    case LinkState::DISABLED:
        return 100;

    case LinkState::STARTING:
        if (rtl8735b_wifi_start_sta() != RTL8735B_WIFI_OK) {
            _start_failures.fetch_add(1);
            printf("WiFi: STA start failed\n");
            _set_state(LinkState::WAIT_RETRY, now_ms);
            return 10;
        }
        _sta_started = true;
        _set_state(LinkState::CONNECTING, now_ms);
        return 1;

    case LinkState::WAIT_RETRY:
        if ((int32_t)(now_ms - _retry_at_ms) < 0) {
            return 10;
        }
        // wifi_on 尚未成功時重新啟動，之後只重新連線；接著直接執行該狀態的一步。
        _set_state(_sta_started ? LinkState::CONNECTING : LinkState::STARTING, now_ms);
        return _link_step(now_ms);

    case LinkState::CONNECTING: {
        _join_attempts.fetch_add(1);
        printf("WiFi: joining (SSID length %u)\n", (unsigned)rtl8735b_wifi_ssid_len);
        const int join = rtl8735b_wifi_join(rtl8735b_wifi_ssid, rtl8735b_wifi_ssid_len,
                                             rtl8735b_wifi_password, rtl8735b_wifi_password_len);
        bool opened = false;
        if (join == RTL8735B_WIFI_OK) {
            _refresh_address();
            if (_local_address.load() != 0) {
                opened = _open_sockets();
            }
        }
        if (join != RTL8735B_WIFI_OK || _local_address.load() == 0 || !opened) {
            _join_failures.fetch_add(1);
            _close_sockets();
            rtl8735b_wifi_leave();
            printf("WiFi: join failed (%d)\n", join);
            _set_state(LinkState::WAIT_RETRY, now_ms);
            return 10;
        }
        _gcs_known.store(false);
        _clear_tx();
        _last_link_check_ms = now_ms;
        print_ipv4("up, local", _local_address.load(), _local_port);
        _set_state(LinkState::UP, now_ms);
        return 1;
    }

    case LinkState::UP:
        if (now_ms - _last_link_check_ms >= LINK_CHECK_INTERVAL_MS) {
            _last_link_check_ms = now_ms;
            if (!rtl8735b_wifi_is_associated()) {
                _link_lost(now_ms, "disassociated");
                return 10;
            }
            _refresh_address();
            if (_local_address.load() == 0) {
                _link_lost(now_ms, "no address");
                return 10;
            }
        }
        if (_protocol == Protocol::TCP) {
            _service_tcp_accept(now_ms);
            _service_tcp_rx(now_ms);
            _service_tcp_tx();
        } else {
            _service_rx(now_ms);
            _service_tx(now_ms);
        }
        return 1;
    }
    return 10;
}

void WiFiDriver::_drop_tcp_client(const char *reason)
{
    rtl8735b_tcp_close(_fd);
    _fd = -1;
    _gcs_known.store(false);
    printf("WiFi: ground station %s\n", reason);
}

void WiFiDriver::_service_tcp_accept(uint32_t now_ms)
{
    uint32_t address = 0;
    uint16_t port = 0;
    const int fd = rtl8735b_tcp_accept(_listen_fd, &address, &port);
    if (fd < 0) {
        return;   // 沒有等待中的連線；listen socket 的錯誤由連線檢查處理斷線
    }
    if (_fd >= 0) {
        if (_gcs_active(now_ms)) {
            // 地面站活躍期間不讓其他主機接手（包含 RC override），與 UDP 相同。
            rtl8735b_tcp_close(fd);
            _rx_foreign_datagrams.fetch_add(1);
            return;
        }
        _drop_tcp_client("replaced by a new connection");
    }
    _fd = fd;
    _gcs_address.store(address);
    _gcs_port_learned.store(port);
    _gcs_known.store(true);
    _last_gcs_rx_ms = now_ms;
    _gcs_changes.fetch_add(1);
    // 新的地面站不接收上一個連線留下的資料。
    {
        WITH_SEMAPHORE(_read_mutex);
        _readbuf.clear();
    }
    _clear_tx();
    print_ipv4("ground station", address, port);
}

void WiFiDriver::_service_tcp_rx(uint32_t now_ms)
{
    for (uint8_t i = 0; i < MAX_DATAGRAMS_PER_STEP && _fd >= 0; i++) {
        uint32_t space;
        {
            WITH_SEMAPHORE(_read_mutex);
            space = _initialized.load() ? _readbuf.space() : sizeof(_datagram);
        }
        if (space == 0) {
            return;   // 讀取緩衝區滿：留在 socket 裡，由 TCP 流量控制讓對端等待
        }
        const int n = rtl8735b_tcp_recv(_fd, _datagram, (uint16_t)std::min<uint32_t>(space, sizeof(_datagram)));
        if (n == 0) {
            return;
        }
        if (n < 0) {
            _drop_tcp_client(n == RTL8735B_WIFI_ERR_CLOSED ? "disconnected" : "connection error");
            return;
        }
        _last_gcs_rx_ms = now_ms;
        _rx_datagrams.fetch_add(1);
        _rx_bytes.fetch_add(n);
        WITH_SEMAPHORE(_read_mutex);
        if (!_initialized.load()) {
            _rx_dropped_bytes.fetch_add(n);   // 本埠已 end()
            continue;
        }
        _readbuf.write(_datagram, n);
    }
}

void WiFiDriver::_service_tcp_tx()
{
    if (_fd < 0) {
        _clear_tx();   // 沒有地面站：丟棄，連線後只送新的資料
        return;
    }
    for (uint8_t i = 0; i < MAX_DATAGRAMS_PER_STEP; i++) {
        uint16_t n;
        uint32_t generation;
        {
            WITH_SEMAPHORE(_write_mutex);
            n = (uint16_t)std::min<uint32_t>(_writebuf.available(), DATAGRAM_MAX);
            if (n == 0) {
                return;
            }
            _writebuf.peekbytes(_datagram, n);
            generation = _tx_generation;
        }
        const int sent = rtl8735b_tcp_send(_fd, _datagram, n);
        if (sent < 0) {
            _tx_errors.fetch_add(1);
            _drop_tcp_client("connection error");
            return;
        }
        if (sent > 0) {
            WITH_SEMAPHORE(_write_mutex);
            if (generation == _tx_generation) {
                _writebuf.advance(sent);
            }
            _tx_datagrams.fetch_add(1);
            _tx_bytes.fetch_add(sent);
        }
        if (sent < n) {
            return;   // 送出緩衝區滿：剩下的留到下一步
        }
    }
}

#endif // CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
