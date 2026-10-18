/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

namespace RTL8735B {

/*
  WiFiDriver 以 UDP 送出時，遇到緩衝不足的重試判斷。

  - 緩衝不足：rtl8735b_udp_sendto 回 RTL8735B_WIFI_ERR_NOBUFS（lwIP 的 ENOBUFS、ENOMEM）。
    Wi-Fi 驅動的封包緩衝所有 socket 共用，其他流量（例如影像串流）一多就會暫時用完；這時資料包不推進，
    留在送出緩衝區，下一步再送同一個。
  - 驅動判定介面沒起來時也回 ENOBUFS，分不出來，一樣重試：從第一次失敗起 RETRY_MS 或 RETRY_MAX 次，
    先到者為準，到了才丟掉。真的斷線時由 WiFiDriver 的連線檢查關 socket、呼叫 reset()。
  - RETRY_MS 500：實測上同時送影像串流時，驅動拒絕送出的時間大多在 500 ms 以內。重試期間後面的訊息
    留在送出緩衝區（WiFiDriver::TX_BUFFER_SIZE），要裝得下 500 ms 的 MAVLink。APM_WIFI task 每步約 1 ms，
    RETRY_MAX 跟著設 500，兩者約同時到。
  - 其他失敗（例如送出部分位元組或別的錯誤）直接丟掉，不重試。
  - 送出緩衝區被清空時（_tx_generation 改變），重試中的那一個資料包已經不在，放棄重試。

  只做判斷，不碰 socket 與緩衝區；只由 APM_WIFI task 使用。主機測試直接用它。
 */
class WiFiTxRetry
{
public:
    static constexpr uint32_t RETRY_MS = 500;
    static constexpr uint16_t RETRY_MAX = 500;

    enum class Action : uint8_t {
        SENT,          // 送出：推進緩衝區
        SENT_RETRIED,  // 重試之後送出：推進緩衝區，計入重試成功
        KEEP,          // 留著：不推進，這一步結束，下一步再送同一個資料包
        DROP,          // 丟掉：推進緩衝區，計入錯誤
    };

    // 送出前呼叫：generation 是這一次取資料包時讀到的 _tx_generation。
    // 和重試開始時不同，表示緩衝區清空過，重試中的資料包已經不在
    void check_generation(uint32_t generation)
    {
        if (_active && generation != _generation) {
            _active = false;
        }
    }

    // 一次送出的結果。sent_ok：整個資料包送出；nobufs：失敗原因是緩衝不足
    Action on_result(bool sent_ok, bool nobufs, uint32_t now_ms, uint32_t generation)
    {
        if (sent_ok) {
            const bool retried = _active;
            _active = false;
            return retried ? Action::SENT_RETRIED : Action::SENT;
        }
        if (!nobufs) {
            _active = false;
            return Action::DROP;
        }
        if (!_active || generation != _generation) {
            _active = true;
            _tries = 1;
            _since_ms = now_ms;
            _generation = generation;
            return Action::KEEP;
        }
        _tries++;
        if ((uint32_t)(now_ms - _since_ms) >= RETRY_MS || _tries > RETRY_MAX) {
            _active = false;
            return Action::DROP;
        }
        return Action::KEEP;
    }

    // socket 關閉時呼叫（斷線）
    void reset() { _active = false; }

    bool active() const { return _active; }

private:
    bool _active{false};
    uint16_t _tries{0};
    uint32_t _since_ms{0};
    uint32_t _generation{0};
};

}
