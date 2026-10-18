/* WiFiDriver（UDP 與 TCP）的主機測試：以替身 shim 驗證狀態機、緩衝、對端學習、TCP 連線管理、斷線重連，
 * 以及主迴圈 API 在 SDK 連線阻塞時仍立即返回。不模擬無線電與 lwIP 時序。 */

#include <AP_HAL/AP_HAL.h>

#include "WiFiDriver.h"
#include "wifi_shim.h"

#include <GCS_MAVLink/GCS.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using RTL8735B::WiFiCoalesce;
using RTL8735B::WiFiDriver;
using LinkState = WiFiDriver::LinkState;

namespace {

constexpr uint32_t kLocalIp = 0xc0a80117u;     // 192.168.1.23
constexpr uint32_t kMask = 0xffffff00u;
constexpr uint32_t kBroadcast = 0xc0a801ffu;
constexpr uint32_t kGcs = 0xc0a80164u;          // 192.168.1.100
constexpr uint32_t kOther = 0xc0a80165u;
constexpr uint16_t kLocalPort = 14555;
constexpr uint16_t kGcsPort = 14550;

struct Datagram {
    uint32_t address;
    uint16_t port;
    std::vector<uint8_t> data;
};

struct FakeNet {
    int start_result = RTL8735B_WIFI_OK;
    int join_result = RTL8735B_WIFI_OK;
    int open_result = 5;
    int associated = 1;
    uint32_t address = kLocalIp;
    uint32_t netmask = kMask;
    int send_result_override = 0;   // < 0 表示送出失敗
    int nobufs_sends = 0;           // 接下來幾次 UDP 送出回緩衝不足；< 0 表示一直回
    uint32_t start_calls = 0;
    uint32_t join_calls = 0;
    uint32_t leave_calls = 0;
    uint32_t open_calls = 0;
    uint32_t close_calls = 0;
    uint16_t opened_port = 0;
    std::deque<Datagram> inbound;
    std::vector<Datagram> sent;
    std::vector<std::vector<uint8_t>> attempts;   // 每一次 UDP 送出的內容，含失敗的
    std::atomic<uint32_t> sdk_calls{0};
    // 讓 join 阻塞，模擬 SDK 同步連線
    bool block_join = false;
    std::mutex join_mutex;
    std::condition_variable join_cv;
    bool join_entered = false;
    bool join_release = false;
    uint32_t created_tasks = 0;
    uint32_t task_priority = 0;
    uint32_t task_stack = 0;
    BaseType_t task_create_result = pdPASS;
    // TCP：listen socket 與各連線
    struct Conn {
        uint32_t address = 0;
        uint16_t port = 0;
        std::deque<uint8_t> inbound;
        bool peer_closed = false;   // 對端關閉：沒有資料後回 ERR_CLOSED
        bool error = false;         // 接收回 ERR_SOCKET
        bool send_error = false;    // 送出回 ERR_SOCKET
        size_t send_room = SIZE_MAX;
        std::vector<uint8_t> sent;
        std::vector<uint16_t> recv_lengths;
        bool open = true;
    };
    int listen_result = 6;
    uint32_t listen_calls = 0;
    uint16_t listen_port = 0;
    std::deque<int> pending;
    std::map<int, Conn> conns;
    std::vector<int> tcp_closed;
};

FakeNet *net;

void reset(FakeNet &fresh)
{
    net = &fresh;
}

std::vector<uint8_t> mav2(uint32_t msgid, uint8_t payload_len, uint8_t fill = 0x55)
{
    std::vector<uint8_t> f = {253, payload_len, 0, 0, 1, 1, 1,
                              (uint8_t)(msgid & 0xff), (uint8_t)((msgid >> 8) & 0xff), (uint8_t)(msgid >> 16)};
    for (uint8_t i = 0; i < payload_len; i++) {
        f.push_back(fill);
    }
    f.push_back(0x12);
    f.push_back(0x34);
    return f;
}

std::vector<uint8_t> mav1(uint8_t msgid, uint8_t payload_len)
{
    std::vector<uint8_t> f = {254, payload_len, 0, 1, 1, msgid};
    for (uint8_t i = 0; i < payload_len; i++) {
        f.push_back(0x66);
    }
    f.push_back(0x12);
    f.push_back(0x34);
    return f;
}

// 幾則訊框依序接起來（湊進同一個資料包的內容）
std::vector<uint8_t> cat(std::initializer_list<std::vector<uint8_t>> frames)
{
    std::vector<uint8_t> out;
    for (const auto &f : frames) {
        out.insert(out.end(), f.begin(), f.end());
    }
    return out;
}

void write_all(WiFiDriver &d, const std::vector<uint8_t> &data)
{
    assert(d.write(data.data(), data.size()) == data.size());
}

// 執行 f，回傳這段期間寫到 stdout（板上是 LOG UART 主控台）的內容
template <typename F>
std::string capture_stdout(F f)
{
    fflush(stdout);
    FILE *tmp = tmpfile();
    assert(tmp != nullptr);
    const int saved = dup(STDOUT_FILENO);
    assert(saved >= 0 && dup2(fileno(tmp), STDOUT_FILENO) >= 0);
    f();
    fflush(stdout);
    assert(dup2(saved, STDOUT_FILENO) >= 0);
    close(saved);
    std::string text;
    rewind(tmp);
    for (int c; (c = fgetc(tmp)) != EOF;) {
        text.push_back((char)c);
    }
    fclose(tmp);
    return text;
}

// 把 driver 帶到 UP；回傳時間
uint32_t bring_up(WiFiDriver &d, uint32_t now = 0)
{
    d.begin(57600);
    d._link_step(now);        // STARTING -> CONNECTING
    d._link_step(now);        // CONNECTING -> UP
    assert(d.status().state == LinkState::UP);
    return now;
}

void test_without_credentials_driver_is_disabled()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    assert(d.status().state == LinkState::NOT_STARTED);
    d.begin(57600);
    assert(!d.is_initialized());
    assert(d.status().state == LinkState::DISABLED);
    assert(fake.created_tasks == 0);
    const uint8_t b[3] = {1, 2, 3};
    assert(d.write(b, sizeof(b)) == 0);
    assert(d.txspace() == 0);
    assert(d.available() == 0);
    assert(d.is_network_port());
    assert(fake.sdk_calls == 0);
}

void test_begin_creates_one_task_and_buffers()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    d.begin(57600);
    assert(d.is_initialized());
    assert(fake.created_tasks == 1);
    assert(fake.task_priority == WiFiDriver::TASK_PRIO);
    assert(fake.task_stack == WiFiDriver::TASK_STACK_WORDS);
    // 緩衝不足重試的 500 ms 期間，後面的 MAVLink 留在送出緩衝區（200 Hz HIGHRES_IMU 約 15 KB/s）
    assert(WiFiDriver::TX_BUFFER_SIZE == 16384);
    assert(d.txspace() >= WiFiDriver::TX_BUFFER_SIZE);
    d.begin(115200, 512, 512);
    assert(fake.created_tasks == 1);
    assert(d.status().state == LinkState::STARTING);
    // begin 只建立 task，不在呼叫端做任何 SDK 呼叫
    assert(fake.sdk_calls == 0);

    FakeNet failing;
    reset(failing);
    failing.task_create_result = pdFAIL;
    WiFiDriver e(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    e.begin(57600);
    assert(!e.is_initialized());
    assert(e.status().state == LinkState::DISABLED);
}

void test_start_and_join_failures_retry()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    d.begin(57600);
    fake.start_result = RTL8735B_WIFI_ERR_START;
    d._link_step(0);
    assert(d.status().state == LinkState::WAIT_RETRY && d.status().start_failures == 1);
    d._link_step(WiFiDriver::RETRY_DELAY_MS - 1);
    assert(fake.start_calls == 1);
    fake.start_result = RTL8735B_WIFI_OK;
    d._link_step(WiFiDriver::RETRY_DELAY_MS);
    assert(fake.start_calls == 2 && d.status().state == LinkState::CONNECTING);

    fake.join_result = RTL8735B_WIFI_ERR_JOIN;
    d._link_step(2000);
    assert(d.status().state == LinkState::WAIT_RETRY);
    assert(d.status().join_attempts == 1 && d.status().join_failures == 1);
    assert(fake.leave_calls == 1 && fake.open_calls == 0);
    fake.join_result = RTL8735B_WIFI_OK;
    d._link_step(2000 + WiFiDriver::RETRY_DELAY_MS);   // WAIT_RETRY -> CONNECTING
    d._link_step(2000 + WiFiDriver::RETRY_DELAY_MS);   // CONNECTING -> UP
    assert(d.status().state == LinkState::UP);
    assert(fake.start_calls == 2);   // wifi_on 成功後不重做
    assert(fake.opened_port == kLocalPort);
    assert(d.status().local_address == kLocalIp);

    // socket 開不起來：斷線後重試
    FakeNet f2;
    reset(f2);
    f2.open_result = RTL8735B_WIFI_ERR_SOCKET;
    WiFiDriver e(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    e.begin(57600);
    e._link_step(0);
    e._link_step(0);
    assert(e.status().state == LinkState::WAIT_RETRY && f2.leave_calls == 1);
}

void test_discovery_broadcasts_only_heartbeats()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    d.begin(57600);
    // 連線前寫入的資料在連線時被清掉，不送出過時的遙測
    write_all(d, mav2(0, 9));
    d._link_step(0);
    d._link_step(0);
    assert(fake.sent.empty() && !d.tx_pending());
    const uint32_t stale_dropped = d.status().tx_dropped_bytes;
    assert(stale_dropped == mav2(0, 9).size());

    const auto hb = mav2(0, 9);
    const auto attitude = mav2(30, 28);
    const auto hb1 = mav1(0, 9);
    write_all(d, attitude);
    write_all(d, hb);
    write_all(d, hb1);
    d._link_step(10);
    assert(fake.sent.size() == 2);
    assert(fake.sent[0].address == kBroadcast && fake.sent[0].port == kGcsPort && fake.sent[0].data == hb);
    assert(fake.sent[1].address == kBroadcast && fake.sent[1].data == hb1);
    assert(d.status().tx_dropped_bytes - stale_dropped == attitude.size());
    assert(d.status().tx_datagrams == 2 && d.status().tx_errors == 0);
    assert(!d.tx_pending());
    assert(!d.status().gcs_known);
}

void test_gcs_learning_unicast_and_rx()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    const auto from_gcs = mav2(0, 9, 0x11);
    fake.inbound.push_back({kGcs, 14550, from_gcs});
    d._link_step(100);
    auto s = d.status();
    assert(s.gcs_known && s.gcs_address == kGcs && s.gcs_port == 14550 && s.gcs_changes == 1);
    assert(d.available() == from_gcs.size());
    uint8_t buf[64];
    assert(d.read(buf, sizeof(buf)) == (ssize_t)from_gcs.size());
    assert(memcmp(buf, from_gcs.data(), from_gcs.size()) == 0);

    // 地面站已知：訊框依序湊進同一個資料包單播；湊不滿時從第一次看到起等 COALESCE_MS 才送
    const auto a = mav2(30, 28), b = mav2(33, 28), c = mav2(0, 9);
    write_all(d, a);
    write_all(d, b);
    write_all(d, c);
    d._link_step(110);
    d._link_step(110 + WiFiCoalesce::COALESCE_MS - 1);
    assert(fake.sent.empty());
    d._link_step(110 + WiFiCoalesce::COALESCE_MS);
    assert(fake.sent.size() == 1);
    assert(fake.sent[0].address == kGcs && fake.sent[0].port == 14550 && fake.sent[0].data == cat({a, b, c}));
    assert(d.status().tx_datagrams == 1 && d.status().tx_bytes == a.size() + b.size() + c.size());

    // 不完整的訊框留在緩衝區，補齊後才送
    const auto big = mav2(24, 30);
    fake.sent.clear();
    assert(d.write(big.data(), 10) == 10);
    d._link_step(130);
    assert(fake.sent.empty() && d.tx_pending());
    assert(d.write(big.data() + 10, big.size() - 10) == big.size() - 10);
    d._link_step(140);
    d._link_step(140 + WiFiCoalesce::COALESCE_MS);
    assert(fake.sent.size() == 1 && fake.sent[0].data == big);
}

void test_foreign_sources_and_gcs_timeout()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    fake.inbound.push_back({kGcs, 14550, {1, 2, 3}});
    d._link_step(1000);
    uint8_t buf[16];
    assert(d.read(buf, sizeof(buf)) == 3);

    // 本機自己的廣播與其他主機的封包都不進讀取緩衝區
    fake.inbound.push_back({kLocalIp, kLocalPort, {9, 9}});
    fake.inbound.push_back({kOther, 14550, {7, 7, 7}});
    d._link_step(1000 + WiFiDriver::PEER_TIMEOUT_MS);
    assert(d.available() == 0);
    assert(d.status().rx_foreign_datagrams == 1);
    assert(d.status().gcs_address == kGcs);

    // 地面站靜默超過逾時：回到廣播，只送 HEARTBEAT
    const uint32_t stale = 1001 + WiFiDriver::PEER_TIMEOUT_MS;
    write_all(d, mav2(30, 28));
    write_all(d, mav2(0, 9));
    const std::string console = capture_stdout([&] { d._link_step(stale); });
    assert(console.find("WiFi: ground station silent, broadcasting heartbeats\n") != std::string::npos);
    assert(!d.status().gcs_known);
    assert(fake.sent.size() == 1 && fake.sent[0].address == kBroadcast);

    // 逾時後第一個送來封包的來源成為新地面站
    fake.inbound.push_back({kOther, 14551, {4, 5}});
    d._link_step(stale + 1);
    auto s = d.status();
    assert(s.gcs_known && s.gcs_address == kOther && s.gcs_port == 14551 && s.gcs_changes == 2);
    assert(d.read(buf, sizeof(buf)) == 2 && buf[0] == 4);
}

void test_rx_overflow_drops_whole_datagram()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    std::vector<uint8_t> big(1400, 0xaa);
    for (int i = 0; i < 3; i++) {
        fake.inbound.push_back({kGcs, 14550, big});
    }
    d._link_step(10);
    assert(d.available() == 2800);
    fake.inbound.push_back({kGcs, 14550, big});
    d._link_step(11);
    assert(d.available() == 2800);
    assert(d.status().rx_dropped_bytes == 1400 + 1400);
    assert(d.discard_input());
    assert(d.available() == 0);
}

void test_send_failure_drops_frame_and_stalled_frame_is_resynced()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    fake.inbound.push_back({kGcs, 14550, {1}});
    d._link_step(1);
    fake.send_result_override = -1;
    const auto a = mav2(30, 28);
    write_all(d, a);
    d._link_step(2);
    d._link_step(2 + WiFiCoalesce::COALESCE_MS);
    assert(!d.tx_pending());
    assert(d.status().tx_errors == 1 && d.status().tx_dropped_bytes == a.size());
    fake.send_result_override = 0;

    // 開頭卡著一個永遠補不齊的訊框：逾時後丟位元組重新對齊，後面的訊框仍送得出去
    // （丟掉開頭後剩下的幾個位元組不是訊框，會先單獨送出）
    fake.sent.clear();
    const auto partial = mav2(24, 30);
    assert(d.write(partial.data(), 5) == 5);
    d._link_step(20);
    assert(fake.sent.empty());
    const auto next = mav2(0, 9);
    write_all(d, next);
    uint32_t now = 20;
    for (int i = 0; i < 20 && (fake.sent.empty() || fake.sent.back().data != next); i++) {
        now += WiFiDriver::TX_STALL_MS + 1;
        fake.inbound.push_back({kGcs, 14550, {1}});
        d._link_step(now);
    }
    assert(!fake.sent.empty() && fake.sent.back().data == next);
}

// 開頭的不完整訊框快等到逾時時用 end()、begin() 重啟埠：緩衝區已清空，之後寫入的新訊框前段
// 要從頭起算等待，不能沿用舊訊框的起始時間而被誤丟第一個位元組；訊框補齊後原樣送出。
void test_restart_restarts_incomplete_frame_wait()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    fake.inbound.push_back({kGcs, kGcsPort, {1}});
    d._link_step(100);
    assert(d.status().gcs_known);

    // 舊訊框只寫了前幾個位元組：時間走到快逾時但還沒超過，沒有送出也沒有丟棄。
    const auto old_frame = mav2(24, 30);
    assert(d.write(old_frame.data(), 5) == 5);
    const uint32_t wait_started = 110;
    d._link_step(wait_started);
    d._link_step(wait_started + WiFiDriver::TX_STALL_MS - 100);
    assert(fake.sent.empty() && d.tx_pending() && d.status().tx_dropped_bytes == 0);

    // 重啟埠之後馬上寫入另一個新訊框的前段（中間沒有任何一步看過空的緩衝區）。
    d.end();
    d.begin(57600);
    assert(d.is_initialized() && !d.tx_pending());
    const auto fresh = mav2(0, 9);
    assert(d.write(fresh.data(), 4) == 4);

    // 這時間點如果沿用舊計時就已經超過 TX_STALL_MS；新訊框的前段不能因此被丟。
    const uint32_t restarted = wait_started + WiFiDriver::TX_STALL_MS + 1;
    d._link_step(restarted);
    assert(d.status().tx_dropped_bytes == 0);
    assert(fake.sent.empty() && d.tx_pending());

    // 補齊其餘部分後，送出的就是完整的新訊框。
    assert(d.write(fresh.data() + 4, fresh.size() - 4) == fresh.size() - 4);
    d._link_step(restarted + 50);
    d._link_step(restarted + 50 + WiFiCoalesce::COALESCE_MS);
    assert(fake.sent.size() == 1);
    assert(fake.sent[0].address == kGcs && fake.sent[0].port == kGcsPort && fake.sent[0].data == fresh);
    assert(d.status().tx_dropped_bytes == 0 && d.status().tx_errors == 0);
    assert(!d.tx_pending());
}

// 每次往前 1 ms 走一步，直到驅動又做了一次 UDP 送出；回傳那一步的時間
uint32_t step_until_send(WiFiDriver &d, uint32_t now)
{
    const size_t before = net->attempts.size();
    for (int i = 0; i < 100 && net->attempts.size() == before; i++) {
        d._link_step(++now);
    }
    assert(net->attempts.size() > before);
    return now;
}

// 地面站送一個封包過來，讓驅動學到它；回傳時間
uint32_t learn_gcs(WiFiDriver &d, uint32_t now)
{
    net->inbound.push_back({kGcs, kGcsPort, {1}});
    d._link_step(now);
    assert(d.status().gcs_known);
    return now;
}

// Wi-Fi 驅動的封包緩衝不足：同一個資料包留著，下一步原樣再送，送出後算一次重試成功，不丟資料
void test_nobufs_keeps_datagram_and_resends()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    uint32_t now = learn_gcs(d, 1);
    const auto a = mav2(30, 28);
    fake.nobufs_sends = 3;
    write_all(d, a);
    now = step_until_send(d, now);
    assert(fake.sent.empty() && d.tx_pending());
    for (int i = 0; i < 3; i++) {
        d._link_step(++now);
    }
    assert(fake.attempts.size() == 4);
    for (const auto &attempt : fake.attempts) {
        assert(attempt == a);
    }
    assert(fake.sent.size() == 1 && fake.sent[0].address == kGcs && fake.sent[0].data == a);
    const WiFiDriver::Status s = d.status();
    assert(s.tx_retried == 1 && s.tx_errors == 0 && s.tx_dropped_bytes == 0);
    assert(s.tx_datagrams == 1 && s.tx_bytes == a.size());
    assert(!d.tx_pending());
}

// 從第一次被拒起 RETRY_MS 仍送不出：丟掉並計數；下一個資料包重新開始，不算重試
void test_nobufs_gives_up_after_retry_ms()
{
    using RTL8735B::WiFiTxRetry;
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    uint32_t now = learn_gcs(d, 1);
    const auto a = mav2(30, 28);
    fake.nobufs_sends = -1;
    write_all(d, a);
    const uint32_t first = step_until_send(d, now);
    now = first;
    while (now < first + WiFiTxRetry::RETRY_MS - 1) {
        d._link_step(++now);
    }
    assert(d.status().tx_errors == 0 && d.tx_pending());
    d._link_step(++now);
    assert(now == first + WiFiTxRetry::RETRY_MS);
    assert(d.status().tx_errors == 1 && d.status().tx_dropped_bytes == a.size());
    assert(!d.tx_pending() && fake.sent.empty());

    fake.nobufs_sends = 0;
    const auto b = mav2(33, 28);
    write_all(d, b);
    step_until_send(d, now);
    assert(fake.sent.size() == 1 && fake.sent[0].data == b);
    assert(d.status().tx_retried == 0 && d.status().tx_errors == 1);
}

// 重試中斷線：重連後只送重連後寫入的資料，斷線前的重試不再算數
void test_link_loss_abandons_retry()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    uint32_t now = learn_gcs(d, 1);
    fake.nobufs_sends = -1;
    write_all(d, mav2(30, 28));
    now = step_until_send(d, now);

    fake.associated = 0;
    now += WiFiDriver::LINK_CHECK_INTERVAL_MS;
    d._link_step(now);
    assert(d.status().state == LinkState::WAIT_RETRY);
    fake.associated = 1;
    fake.nobufs_sends = 0;
    now += WiFiDriver::RETRY_DELAY_MS;
    d._link_step(now);
    d._link_step(now);
    assert(d.status().state == LinkState::UP && !d.tx_pending());

    now = learn_gcs(d, now + 1);
    const auto b = mav2(33, 28);
    write_all(d, b);
    step_until_send(d, now);
    assert(fake.sent.size() == 1 && fake.sent[0].data == b);
    assert(d.status().tx_retried == 0 && d.status().tx_errors == 0);
}

// 湊滿一個資料包（下一則放不下）就立刻送，不等；剩下的照同一段的計時，第一次看到後 COALESCE_MS 才送
void test_full_datagram_is_sent_without_waiting()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    learn_gcs(d, 100);
    const auto imu = mav2(105, 62);   // 74 bytes，約一則 HIGHRES_IMU
    const size_t per_datagram = WiFiDriver::DATAGRAM_MAX / imu.size();
    assert(per_datagram == 19);
    for (size_t i = 0; i < per_datagram + 6; i++) {
        write_all(d, imu);
    }
    d._link_step(110);
    assert(fake.sent.size() == 1 && fake.sent[0].data.size() == per_datagram * imu.size());
    d._link_step(110 + WiFiCoalesce::COALESCE_MS - 1);
    assert(fake.sent.size() == 1);
    d._link_step(110 + WiFiCoalesce::COALESCE_MS);
    assert(fake.sent.size() == 2 && fake.sent[1].data.size() == 6 * imu.size());
    assert(!d.tx_pending() && d.status().tx_datagrams == 2);
}

// 湊好的資料包遇到緩衝不足：重送時從同一則開頭重新湊，重試中新到的訊息一起接上；
// 重試滿 RETRY_MS 仍被拒就整包丟掉
void test_packed_datagram_retry_grows_then_drops_whole()
{
    using RTL8735B::WiFiTxRetry;
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    learn_gcs(d, 100);
    const auto a = mav2(30, 28), b = mav2(33, 28), c = mav2(0, 9);
    write_all(d, a);
    write_all(d, b);
    fake.nobufs_sends = -1;
    d._link_step(110);
    const uint32_t first = 110 + WiFiCoalesce::COALESCE_MS;
    d._link_step(first);
    assert(fake.attempts.size() == 1 && fake.attempts[0] == cat({a, b}));
    write_all(d, c);
    d._link_step(first + 1);
    assert(fake.attempts.size() == 2 && fake.attempts[1] == cat({a, b, c}));
    uint32_t now = first + 1;
    while (now < first + WiFiTxRetry::RETRY_MS - 1) {
        d._link_step(++now);
    }
    assert(d.status().tx_errors == 0 && d.tx_pending());
    d._link_step(++now);
    const WiFiDriver::Status s = d.status();
    assert(s.tx_errors == 1 && s.tx_dropped_bytes == a.size() + b.size() + c.size());
    assert(!d.tx_pending() && fake.sent.empty());
}

void test_link_loss_reconnects()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    fake.inbound.push_back({kGcs, 14550, {1}});
    d._link_step(1);
    assert(d.status().gcs_known);

    fake.associated = 0;
    fake_statustext().clear();
    d._link_step(1 + WiFiDriver::LINK_CHECK_INTERVAL_MS);
    auto s = d.status();
    assert(s.state == LinkState::WAIT_RETRY && s.link_losses == 1 && !s.gcs_known);
    // 斷線也以 STATUSTEXT（警告）送出：Wi-Fi 斷了，地面站仍可能從 SERIAL0 讀到
    assert(fake_statustext().find("[4] WiFi: link lost (") == 0);
    assert(fake.close_calls == 1 && fake.leave_calls == 1);

    // 斷線期間主迴圈照常寫入，不阻塞；重連時清掉
    write_all(d, mav2(30, 28));
    fake.associated = 1;
    const uint32_t t = 1 + WiFiDriver::LINK_CHECK_INTERVAL_MS + WiFiDriver::RETRY_DELAY_MS;
    d._link_step(t);
    d._link_step(t);
    assert(d.status().state == LinkState::UP && fake.open_calls == 2);
    assert(!d.tx_pending());

    // 位址消失（DHCP 失效）也視為斷線
    fake.address = 0;
    d._link_step(t + WiFiDriver::LINK_CHECK_INTERVAL_MS);
    assert(d.status().state == LinkState::WAIT_RETRY && d.status().link_losses == 2);
}

void test_end_keeps_task_and_rebegin()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    bring_up(d);
    d.end();
    assert(!d.is_initialized());
    const uint8_t b = 1;
    assert(d.write(&b, 1) == 0);
    fake.inbound.push_back({kGcs, 14550, {1, 2}});
    d._link_step(5);
    assert(d.available() == 0);
    d.begin(57600);
    assert(d.is_initialized() && fake.created_tasks == 1);
    assert(d.status().state == LinkState::UP);
}

void test_uart_api_does_not_block_while_sdk_join_blocks()
{
    FakeNet fake;
    reset(fake);
    fake.block_join = true;
    WiFiDriver d(WiFiDriver::Protocol::UDP, kLocalPort, kGcsPort);
    d.begin(57600);
    d._link_step(0);   // STARTING -> CONNECTING
    std::thread task([&]() { d._link_step(1); });
    {
        std::unique_lock<std::mutex> lock(fake.join_mutex);
        fake.join_cv.wait(lock, [&]() { return fake.join_entered; });
    }
    const uint32_t calls_before = fake.sdk_calls;
    const auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t> chunk(100, 0x42);
    size_t accepted = 0;
    for (int i = 0; i < 200; i++) {
        accepted += d.write(chunk.data(), chunk.size());
        (void)d.txspace();
        (void)d.available();
        uint8_t tmp[8];
        (void)d.read(tmp, sizeof(tmp));
        (void)d.tx_pending();
        (void)d.status();
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    // 緩衝區滿了就少收，但不等待
    assert(accepted <= WiFiDriver::TX_BUFFER_SIZE && accepted > 0);
    assert(elapsed < std::chrono::milliseconds(200));
    assert(fake.sdk_calls == calls_before);   // 主迴圈端沒有呼叫任何 SDK／網路函式
    {
        std::lock_guard<std::mutex> lock(fake.join_mutex);
        fake.join_release = true;
    }
    fake.join_cv.notify_all();
    task.join();
    assert(d.status().state == LinkState::UP);
}

// ---- TCP ----

constexpr uint16_t kTcpPort = 5760;
constexpr int kClientA = 20;
constexpr int kClientB = 21;

void add_client(int fd, uint32_t address, uint16_t port)
{
    FakeNet::Conn c;
    c.address = address;
    c.port = port;
    net->conns[fd] = c;
    net->pending.push_back(fd);
}

void push(int fd, const std::vector<uint8_t> &data)
{
    std::deque<uint8_t> &q = net->conns.at(fd).inbound;
    q.insert(q.end(), data.begin(), data.end());
}

bool closed(int fd)
{
    for (int c : net->tcp_closed) {
        if (c == fd) {
            return true;
        }
    }
    return false;
}

void test_tcp_listens_instead_of_udp()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    bring_up(d);
    assert(fake.listen_calls == 1 && fake.listen_port == kTcpPort);
    assert(fake.open_calls == 0);
    assert(!d.status().gcs_known);
}

void test_tcp_accepts_client_and_exchanges_data()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    add_client(kClientA, kGcs, 50000);
    push(kClientA, {1, 2, 3, 4, 5});
    d._link_step(now += 1);
    const WiFiDriver::Status s = d.status();
    assert(s.gcs_known && s.gcs_address == kGcs && s.gcs_port == 50000 && s.gcs_changes == 1);
    uint8_t buf[8];
    assert(d.available() == 5);
    assert(d.read(buf, sizeof(buf)) == 5 && buf[0] == 1 && buf[4] == 5);

    const std::vector<uint8_t> out = {9, 8, 7};
    write_all(d, out);
    d._link_step(now += 1);
    assert(fake.conns.at(kClientA).sent == out);
    assert(!d.tx_pending());
}

void test_tcp_without_client_drops_tx_and_sends_only_new_data()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    write_all(d, {1, 1, 1, 1});
    d._link_step(now += 1);
    assert(!d.tx_pending() && d.status().tx_dropped_bytes == 4);

    add_client(kClientA, kGcs, 50000);
    d._link_step(now += 1);
    write_all(d, {2, 2});
    d._link_step(now += 1);
    assert(fake.conns.at(kClientA).sent == std::vector<uint8_t>({2, 2}));
}

void test_tcp_active_gcs_rejects_new_connection()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    add_client(kClientA, kGcs, 50000);
    push(kClientA, {1});
    d._link_step(now += 1);

    // 地面站活躍期間（3 秒內有資料）另一台主機連入：立刻關閉，原地面站照常服務
    now += WiFiDriver::PEER_TIMEOUT_MS - 1;
    add_client(kClientB, kOther, 50001);
    d._link_step(now);
    assert(closed(kClientB) && !closed(kClientA));
    assert(d.status().rx_foreign_datagrams == 1 && d.status().gcs_address == kGcs);
    push(kClientA, {2});
    d._link_step(now += 1);
    uint8_t buf[4];
    assert(d.read(buf, sizeof(buf)) == 2 && buf[1] == 2);
}

void test_tcp_silent_gcs_is_replaced_by_new_connection()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    add_client(kClientA, kGcs, 50000);
    push(kClientA, {1, 2, 3});
    d._link_step(now += 1);
    write_all(d, {7, 7});   // 留在 TX 緩衝區，新連線不應收到

    // 舊連線靜默超過 3 秒（地面站重開或換線，舊連線還沒被偵測為斷線）：新的連線取代它
    now += WiFiDriver::PEER_TIMEOUT_MS + 1;
    fake.conns.at(kClientA).send_room = 0;   // 舊連線送不出，資料留在緩衝區
    add_client(kClientB, kOther, 50001);
    d._link_step(now);
    assert(closed(kClientA) && !closed(kClientB));
    const WiFiDriver::Status s = d.status();
    assert(s.gcs_known && s.gcs_address == kOther && s.gcs_changes == 2);
    // 上一個地面站留下的收發資料都清掉
    assert(d.available() == 0 && !d.tx_pending());
    write_all(d, {5});
    d._link_step(now += 1);
    assert(fake.conns.at(kClientB).sent == std::vector<uint8_t>({5}));
}

void test_tcp_peer_close_and_errors_drop_client()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    add_client(kClientA, kGcs, 50000);
    d._link_step(now += 1);
    fake.conns.at(kClientA).peer_closed = true;
    d._link_step(now += 1);
    assert(closed(kClientA) && !d.status().gcs_known);

    // 沒有地面站時可以立刻接受下一個連線
    add_client(kClientB, kOther, 50001);
    d._link_step(now += 1);
    assert(d.status().gcs_known && d.status().gcs_address == kOther);

    // 接收錯誤：放掉連線，不算送出錯誤
    fake.conns.at(kClientB).error = true;
    d._link_step(now += 1);
    assert(closed(kClientB) && !d.status().gcs_known && d.status().tx_errors == 0);

    // 送出錯誤：計數並放掉連線
    const int kClientC = 22;
    add_client(kClientC, kGcs, 50002);
    d._link_step(now += 1);
    assert(d.status().gcs_known);
    write_all(d, {1, 2});
    fake.conns.at(kClientC).send_error = true;
    d._link_step(now += 1);
    assert(closed(kClientC) && !d.status().gcs_known && d.status().tx_errors == 1);
}

void test_tcp_reads_only_free_space()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    add_client(kClientA, kGcs, 50000);
    d._link_step(now += 1);
    const uint32_t space = d.status().state == LinkState::UP ? WiFiDriver::RX_BUFFER_SIZE : 0;
    std::vector<uint8_t> big(space + 1000);
    for (size_t i = 0; i < big.size(); i++) {
        big[i] = (uint8_t)i;
    }
    push(kClientA, big);
    for (int i = 0; i < 10; i++) {
        d._link_step(now += 1);
    }
    // 緩衝區滿時資料留在 socket，由 TCP 流量控制讓對端等待，不丟資料
    assert(d.available() == space);
    assert(fake.conns.at(kClientA).inbound.size() == 1000);
    assert(d.status().rx_dropped_bytes == 0);
    for (uint16_t n : fake.conns.at(kClientA).recv_lengths) {
        assert(n <= WiFiDriver::DATAGRAM_MAX);
    }
    std::vector<uint8_t> got(big.size());
    size_t total = 0;
    while (total < big.size()) {
        const ssize_t n = d.read(got.data() + total, 512);
        if (n > 0) {
            total += (size_t)n;
        } else {
            d._link_step(now += 1);
        }
    }
    assert(got == big);
}

void test_tcp_partial_send_keeps_the_rest_in_order()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    add_client(kClientA, kGcs, 50000);
    d._link_step(now += 1);
    std::vector<uint8_t> out(30);
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = (uint8_t)(100 + i);
    }
    fake.conns.at(kClientA).send_room = 10;
    write_all(d, out);
    d._link_step(now += 1);
    assert(fake.conns.at(kClientA).sent.size() == 10 && d.tx_pending());
    fake.conns.at(kClientA).send_room = SIZE_MAX;
    d._link_step(now += 1);
    assert(fake.conns.at(kClientA).sent == out && !d.tx_pending());
}

void test_tcp_link_loss_closes_sockets_and_relistens()
{
    FakeNet fake;
    reset(fake);
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    uint32_t now = bring_up(d);
    add_client(kClientA, kGcs, 50000);
    d._link_step(now += 1);
    fake.associated = 0;
    d._link_step(now += WiFiDriver::LINK_CHECK_INTERVAL_MS);
    assert(d.status().state == LinkState::WAIT_RETRY);
    assert(closed(kClientA) && closed(fake.listen_result) && !d.status().gcs_known);
    fake.associated = 1;
    d._link_step(now += WiFiDriver::RETRY_DELAY_MS);
    assert(d.status().state == LinkState::UP && fake.listen_calls == 2);
}

void test_tcp_listen_failure_is_a_join_failure()
{
    FakeNet fake;
    reset(fake);
    fake.listen_result = RTL8735B_WIFI_ERR_SOCKET;
    WiFiDriver d(WiFiDriver::Protocol::TCP, kTcpPort, 0);
    d.begin(57600);
    d._link_step(0);
    d._link_step(0);
    assert(d.status().state == LinkState::WAIT_RETRY && d.status().join_failures == 1);
    assert(fake.tcp_closed.empty());   // 沒開成的 socket 不關
}

}  // namespace

/* 替身：shim、FreeRTOS、AP_HAL::millis。帳密由 wifi_test_credentials.c 提供，
 * 以有帳密與沒有帳密兩種設定各編一次。 */
extern "C" {
int rtl8735b_wifi_start_sta(void) { net->sdk_calls++; net->start_calls++; return net->start_result; }
int rtl8735b_wifi_join(const uint8_t *ssid, uint8_t ssid_len, const uint8_t *password, uint8_t password_len)
{
    net->sdk_calls++;
    net->join_calls++;
    assert(ssid_len == 7 && memcmp(ssid, "lab-net", 7) == 0);
    assert(password_len == 8 && memcmp(password, "12345678", 8) == 0);
    if (net->block_join) {
        std::unique_lock<std::mutex> lock(net->join_mutex);
        net->join_entered = true;
        net->join_cv.notify_all();
        net->join_cv.wait(lock, []() { return net->join_release; });
    }
    return net->join_result;
}
int rtl8735b_wifi_is_associated(void) { net->sdk_calls++; return net->associated; }
int rtl8735b_wifi_get_ipv4(uint32_t *address, uint32_t *netmask)
{
    net->sdk_calls++;
    *address = net->address;
    *netmask = net->netmask;
    return net->address == 0 ? RTL8735B_WIFI_ERR_DHCP : RTL8735B_WIFI_OK;
}
void rtl8735b_wifi_leave(void) { net->sdk_calls++; net->leave_calls++; }
int rtl8735b_udp_open(uint16_t port)
{
    net->sdk_calls++;
    net->open_calls++;
    net->opened_port = port;
    return net->open_result;
}
int rtl8735b_udp_recvfrom(int fd, uint8_t *buffer, uint16_t length, uint32_t *address, uint16_t *port)
{
    net->sdk_calls++;
    assert(fd == net->open_result);
    if (net->inbound.empty()) {
        return 0;
    }
    Datagram g = net->inbound.front();
    net->inbound.pop_front();
    assert(g.data.size() <= length);
    memcpy(buffer, g.data.data(), g.data.size());
    *address = g.address;
    *port = g.port;
    return (int)g.data.size();
}
int rtl8735b_udp_sendto(int fd, const uint8_t *buffer, uint16_t length, uint32_t address, uint16_t port)
{
    net->sdk_calls++;
    assert(fd == net->open_result);
    net->attempts.push_back(std::vector<uint8_t>(buffer, buffer + length));
    if (net->nobufs_sends != 0) {
        if (net->nobufs_sends > 0) {
            net->nobufs_sends--;
        }
        return RTL8735B_WIFI_ERR_NOBUFS;
    }
    if (net->send_result_override < 0) {
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    net->sent.push_back({address, port, std::vector<uint8_t>(buffer, buffer + length)});
    return length;
}
void rtl8735b_udp_close(int fd)
{
    net->sdk_calls++;
    assert(fd == net->open_result);
    net->close_calls++;
}
int rtl8735b_tcp_listen(uint16_t port)
{
    net->sdk_calls++;
    net->listen_calls++;
    net->listen_port = port;
    return net->listen_result;
}
int rtl8735b_tcp_accept(int listen_fd, uint32_t *address, uint16_t *port)
{
    net->sdk_calls++;
    assert(listen_fd == net->listen_result);
    if (net->pending.empty()) {
        return RTL8735B_WIFI_NONE;
    }
    const int fd = net->pending.front();
    net->pending.pop_front();
    *address = net->conns.at(fd).address;
    *port = net->conns.at(fd).port;
    return fd;
}
int rtl8735b_tcp_recv(int fd, uint8_t *buffer, uint16_t length)
{
    net->sdk_calls++;
    FakeNet::Conn &c = net->conns.at(fd);
    assert(c.open && length > 0);
    c.recv_lengths.push_back(length);
    if (c.error) {
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    if (c.inbound.empty()) {
        return c.peer_closed ? RTL8735B_WIFI_ERR_CLOSED : 0;
    }
    const size_t n = std::min<size_t>(length, c.inbound.size());
    for (size_t i = 0; i < n; i++) {
        buffer[i] = c.inbound.front();
        c.inbound.pop_front();
    }
    return (int)n;
}
int rtl8735b_tcp_send(int fd, const uint8_t *buffer, uint16_t length)
{
    net->sdk_calls++;
    FakeNet::Conn &c = net->conns.at(fd);
    assert(c.open && length > 0);
    if (c.send_error) {
        return RTL8735B_WIFI_ERR_SOCKET;
    }
    const size_t n = std::min<size_t>(length, c.send_room);
    c.send_room -= n;
    c.sent.insert(c.sent.end(), buffer, buffer + n);
    return (int)n;
}
void rtl8735b_tcp_close(int fd)
{
    net->sdk_calls++;
    assert(fd >= 0);   // 驅動不應以無效 fd 呼叫 SDK
    net->tcp_closed.push_back(fd);
    if (fd != net->listen_result) {
        net->conns.at(fd).open = false;
    }
}

BaseType_t xTaskCreate(TaskFunction_t, const char *name, uint32_t stack_words, void *, UBaseType_t priority,
                       TaskHandle_t *handle)
{
    assert(strcmp(name, "APM_WIFI") == 0);
    if (net->task_create_result != pdPASS) {
        return net->task_create_result;
    }
    net->created_tasks++;
    net->task_priority = priority;
    net->task_stack = stack_words;
    *handle = (TaskHandle_t)0x1;
    return pdPASS;
}
void vTaskDelay(TickType_t) {}
}

uint32_t AP_HAL::millis() { return 0; }

int main()
{
    if (rtl8735b_wifi_ssid_len == 0) {
        test_without_credentials_driver_is_disabled();
        printf("wifi_driver host test (no credentials): PASS\n");
        return 0;
    }
    test_begin_creates_one_task_and_buffers();
    test_start_and_join_failures_retry();
    test_discovery_broadcasts_only_heartbeats();
    test_gcs_learning_unicast_and_rx();
    test_foreign_sources_and_gcs_timeout();
    test_rx_overflow_drops_whole_datagram();
    test_send_failure_drops_frame_and_stalled_frame_is_resynced();
    test_restart_restarts_incomplete_frame_wait();
    test_nobufs_keeps_datagram_and_resends();
    test_nobufs_gives_up_after_retry_ms();
    test_link_loss_abandons_retry();
    test_full_datagram_is_sent_without_waiting();
    test_packed_datagram_retry_grows_then_drops_whole();
    test_link_loss_reconnects();
    test_end_keeps_task_and_rebegin();
    test_uart_api_does_not_block_while_sdk_join_blocks();
    test_tcp_listens_instead_of_udp();
    test_tcp_accepts_client_and_exchanges_data();
    test_tcp_without_client_drops_tx_and_sends_only_new_data();
    test_tcp_active_gcs_rejects_new_connection();
    test_tcp_silent_gcs_is_replaced_by_new_connection();
    test_tcp_peer_close_and_errors_drop_client();
    test_tcp_reads_only_free_space();
    test_tcp_partial_send_keeps_the_rest_in_order();
    test_tcp_link_loss_closes_sockets_and_relistens();
    test_tcp_listen_failure_is_a_join_failure();
    printf("wifi_driver host test (UDP and TCP): PASS\n");
    return 0;
}
