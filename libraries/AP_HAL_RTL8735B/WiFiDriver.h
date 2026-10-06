/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <AP_HAL/UARTDriver.h>
#include <AP_HAL/utility/RingBuffer.h>
#include <AP_HAL_RTL8735B/Semaphores.h>

#include <FreeRTOS.h>
#include <task.h>

#include <atomic>
#include <stdint.h>

namespace RTL8735B {

/*
  Wi-Fi STA 上承載 MAVLink 的序列埠，傳輸層在建置時選 UDP 或 TCP server。

  - 連線、DHCP、收發都在獨立的 APM_WIFI task（_link_step）；主迴圈呼叫的 UART API 只碰環形緩衝區與
    短暫持有的 mutex，不呼叫任何網路或 SDK 函式，所以不會因為 Wi-Fi 未連線、重連或送不出而阻塞。
  - 斷線（與 AP 失聯或位址消失）時關閉 socket，RETRY_DELAY_MS 後重新連線，不限次數。
  - 沒有建置時注入的帳密（rtl8735b_wifi_ssid_len 為 0）時不啟動 Wi-Fi，is_initialized() 為 false。

  UDP：
  - 本機綁定 local_port；還沒有地面站時，只把 HEARTBEAT 廣播到子網路廣播位址的 gcs_port，其他訊息丟棄
    （廣播在 802.11 沒有確認與重送，且以基本速率送出，只用來讓地面站發現本機）。
  - 收到第一個資料包後把來源位址與埠當成地面站，之後的每個 MAVLink 訊框以一個資料包單播給它；
    地面站活躍期間只接受它的封包，靜默超過 PEER_TIMEOUT_MS 後回到廣播，並接受下一個送來封包的來源。

  TCP：
  - 在 local_port 等待連線，同時只服務一個地面站；沒有地面站時送出的資料丟棄。
  - 地面站活躍期間（PEER_TIMEOUT_MS 內有收到資料），新的連線立刻關閉；地面站靜默超過該時間後，
    新的連線取代舊的（地面站重新連線時，舊連線可能還沒被偵測為斷線）。
  - 接收依讀取緩衝區的空間讀取，緩衝區滿時由 TCP 流量控制讓對端等待，不丟資料。
 */
class WiFiDriver : public AP_HAL::UARTDriver
{
public:
    enum class Protocol : uint8_t {
        UDP,
        TCP,
    };

    enum class LinkState : uint8_t {
        NOT_STARTED = 0,   // 還沒有 begin()
        DISABLED,          // 沒有帳密或無法建立 task
        STARTING,          // 等待 lwIP 初始化與 wifi_on
        CONNECTING,        // 等待連線與 DHCP
        WAIT_RETRY,        // 失敗或斷線後等待重試
        UP,                // 已取得位址、socket 已開
    };

    struct Status {
        LinkState state;
        bool gcs_known;          // 目前有活躍的地面站（TCP：有連線）
        uint32_t local_address;  // 主機位元組序
        uint32_t gcs_address;
        uint16_t gcs_port;
        uint32_t start_failures;
        uint32_t join_attempts;
        uint32_t join_failures;
        uint32_t link_losses;
        uint32_t gcs_changes;         // UDP：換地面站次數；TCP：接受的連線數
        uint32_t rx_datagrams;        // UDP：資料包數；TCP：接收次數
        uint32_t rx_bytes;
        uint32_t rx_dropped_bytes;    // 讀取緩衝區放不下而丟棄（只有 UDP）
        uint32_t rx_foreign_datagrams;// 地面站活躍期間其他來源的資料包（TCP：拒絕的連線）
        uint32_t tx_datagrams;        // UDP：資料包數；TCP：送出次數
        uint32_t tx_bytes;
        uint32_t tx_dropped_bytes;    // 未連線、沒有地面站時的資料，送出失敗或無法成框
        uint32_t tx_errors;
    };

    // Wi-Fi task：優先權與 UART/IO task 相同（Scheduler.h 的 UART_PRIO 3），低於主迴圈 4；
    // SDK 的 Wi-Fi 與 lwIP task 為 5、6、9。
    static constexpr UBaseType_t TASK_PRIO = 3;
    static constexpr uint32_t TASK_STACK_WORDS = 8192 / sizeof(StackType_t);
    static constexpr uint32_t RX_BUFFER_SIZE = 4096;
    static constexpr uint32_t TX_BUFFER_SIZE = 8192;
    static constexpr uint32_t PEER_TIMEOUT_MS = 3000;     // 換地面站的條件
    static constexpr uint32_t RETRY_DELAY_MS = 1000;
    static constexpr uint32_t LINK_CHECK_INTERVAL_MS = 100;
    static constexpr uint32_t TX_STALL_MS = 500;          // 緩衝區開頭的不完整訊框超過此時間就丟一個位元組重新對齊
    static constexpr uint8_t MAX_DATAGRAMS_PER_STEP = 16;
    static constexpr uint16_t DATAGRAM_MAX = 1472;        // 乙太網路 MTU 1500 減 IPv4 與 UDP 標頭

    // UDP：local_port 為本機埠、gcs_port 為廣播 HEARTBEAT 的目的埠；TCP：local_port 為等待連線的埠，gcs_port 不用。
    WiFiDriver(Protocol protocol, uint16_t local_port, uint16_t gcs_port);

    bool is_initialized() override;
    bool tx_pending() override;
    uint32_t txspace() override;
    // TODO(rtl8735b): 實際吞吐量未量測；此值只影響參數與 FTP 的送出節奏，實際送出仍受 txspace 限制。
    uint32_t bw_in_bytes_per_second() const override { return 100000; }
    bool is_network_port() const override { return true; }

    Status status() const;

    // APM_WIFI task 的一步；回傳建議的下一步延遲（ms）。連線步驟會阻塞在 SDK 裡，只能在該 task 或測試中呼叫。
    uint32_t _link_step(uint32_t now_ms);

protected:
    void _begin(uint32_t baud, uint16_t rx_space, uint16_t tx_space) override;
    void _end() override;
    void _flush() override;
    uint32_t _available() override;
    ssize_t _read(uint8_t *buffer, uint16_t count) override;
    size_t _write(const uint8_t *buffer, size_t size) override;
    bool _discard_input() override;

#if HAL_UART_STATS_ENABLED
    uint32_t get_total_tx_bytes() const override { return _tx_bytes.load(); }
    uint32_t get_total_rx_bytes() const override { return _rx_bytes.load(); }
    uint32_t get_total_dropped_rx_bytes() const override { return _rx_dropped_bytes.load(); }
#endif

private:
    static void _task_entry(void *arg);
    void _set_state(LinkState state, uint32_t now_ms);
    bool _open_sockets();
    void _close_sockets();
    void _link_lost(uint32_t now_ms, const char *reason);
    void _refresh_address();
    bool _accept_source(uint32_t address, uint16_t port, uint32_t now_ms);
    bool _gcs_active(uint32_t now_ms) const;
    void _service_rx(uint32_t now_ms);
    void _service_tx(uint32_t now_ms);
    void _service_tcp_accept(uint32_t now_ms);
    void _service_tcp_rx(uint32_t now_ms);
    void _service_tcp_tx();
    void _drop_tcp_client(const char *reason);
    void _clear_tx();

    const Protocol _protocol;
    const uint16_t _local_port;
    const uint16_t _gcs_port;

    std::atomic<LinkState> _state{LinkState::NOT_STARTED};
    std::atomic<bool> _initialized{false};
    TaskHandle_t _task;
    bool _disabled_reported;

    // 以下只由 APM_WIFI task 修改
    int _fd;          // UDP socket，或 TCP 的地面站連線
    int _listen_fd;   // TCP listen socket
    bool _sta_started;
    uint32_t _retry_at_ms;
    uint32_t _last_link_check_ms;
    uint32_t _broadcast_address;
    uint32_t _last_gcs_rx_ms;
    uint32_t _tx_incomplete_since_ms;
    bool _tx_incomplete;
    uint8_t _datagram[DATAGRAM_MAX];

    std::atomic<bool> _gcs_known{false};
    std::atomic<uint32_t> _local_address{0};
    std::atomic<uint32_t> _gcs_address{0};
    std::atomic<uint16_t> _gcs_port_learned{0};

    ByteBuffer _readbuf{0};
    ByteBuffer _writebuf{0};
    mutable Semaphore _read_mutex;
    mutable Semaphore _write_mutex;
    uint32_t _tx_generation;   // _write_mutex 保護；清空 TX 緩衝區時遞增，避免送出後推進到新資料

    std::atomic<uint32_t> _start_failures{0};
    std::atomic<uint32_t> _join_attempts{0};
    std::atomic<uint32_t> _join_failures{0};
    std::atomic<uint32_t> _link_losses{0};
    std::atomic<uint32_t> _gcs_changes{0};
    std::atomic<uint32_t> _rx_datagrams{0};
    std::atomic<uint32_t> _rx_bytes{0};
    std::atomic<uint32_t> _rx_dropped_bytes{0};
    std::atomic<uint32_t> _rx_foreign_datagrams{0};
    std::atomic<uint32_t> _tx_datagrams{0};
    std::atomic<uint32_t> _tx_bytes{0};
    std::atomic<uint32_t> _tx_dropped_bytes{0};
    std::atomic<uint32_t> _tx_errors{0};
};

}
