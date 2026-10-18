/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <stdint.h>

namespace RTL8735B {

/*
  WiFiDriver 以 UDP 送給地面站時，把幾則完整的 MAVLink 湊進同一個資料包。

  - 為什麼：mavlink_packetise 一次只給一則訊息，例如 200 Hz 的 HIGHRES_IMU 就是每秒 200 個資料包，
    每個都要配一個 Wi-Fi 驅動的封包緩衝；驅動緩衝不夠時送出被拒（ENOBUFS，見 WiFiTxRetry.h）。
    湊包讓資料包數降到幾分之一。
  - 怎麼湊：從緩衝區開頭第一則（mavlink_packetise 給的長度）往後接完整的訊框，總長不超過資料包上限。
    只接 MAVLink 1／2 的完整訊框；遇到不是訊框開頭的位元組就停，留給下一個資料包由 mavlink_packetise 處理。
  - 什麼時候送：下一則完整訊框放不下（湊滿）就立刻送；湊不滿時，從這一段忙碌期第一次看到可送的訊息起，
    等滿 COALESCE_MS 就把手上的全部送出（之後一路送到緩衝區沒有完整訊框為止，不再等）。
    所以每則訊息多等的時間不超過 COALESCE_MS（再加 APM_WIFI task 一步約 1 ms）。
  - 心跳廣播（還沒有地面站時）、TCP 不經這裡，照舊一則一包。
  - 重試中（WiFiTxRetry 留著同一個資料包）不等，直接送；每次重送都從同一則開頭重新湊，等待中新到的訊息
    可能一起接上。
  - 和重試合用的最壞情況：一個資料包重試滿 WiFiTxRetry::RETRY_MS 仍被拒就整包丟掉，裡面可能有十幾則訊息
    （資料包上限 1,472 bytes，HIGHRES_IMU 一則約 75 bytes），缺口比一則一包時長。

  只做判斷，不碰 socket 與緩衝區；只由 APM_WIFI task 使用。主機測試直接用它。
 */
class WiFiCoalesce
{
public:
    static constexpr uint32_t COALESCE_MS = 10;

    static constexpr uint8_t STX_V1 = 0xFE;
    static constexpr uint8_t STX_V2 = 0xFD;
    static constexpr uint8_t IFLAG_SIGNED = 0x01;
    static constexpr uint8_t SIGNATURE_LEN = 13;

    // buf 開頭一個訊框的總長；不是訊框開頭回 0。標頭還不夠判斷長度時 *known 設 false（回 0）
    static uint16_t frame_len(const uint8_t *buf, uint16_t n, bool *known)
    {
        *known = false;
        if (n == 0) {
            return 0;
        }
        if (buf[0] == STX_V1) {
            if (n < 2) {
                return 0;
            }
            *known = true;
            return (uint16_t)(buf[1] + 8);
        }
        if (buf[0] == STX_V2) {
            if (n < 3) {
                return 0;
            }
            *known = true;
            return (uint16_t)(buf[1] + 12 + ((buf[2] & IFLAG_SIGNED) ? SIGNATURE_LEN : 0));
        }
        return 0;
    }

    /*
      從 first（mavlink_packetise 給的第一則長度）往後接完整訊框，回傳要送的總長（≥ first）。
      buf、n：從緩衝區開頭偷看到的位元組（n ≤ cap）；more：緩衝區裡 n 之後還有資料（n 被 cap 截斷）。
      *full：下一則放不下或不是 MAVLink，應該現在就送；false 表示手上的都接完了，可以再等後面的訊息。
     */
    static uint16_t extend(const uint8_t *buf, uint16_t n, uint16_t first, bool more, uint16_t cap, bool *full)
    {
        *full = true;
        if (first == 0 || first > n) {
            return first;
        }
        bool known;
        if (frame_len(buf, n, &known) != first) {
            return first;   // 第一段不是完整的 MAVLink 訊框（非 MAVLink 資料）：單獨送
        }
        uint16_t off = first;
        while (true) {
            const uint16_t left = (uint16_t)(n - off);
            if (left == 0) {
                *full = more;   // 截斷在訊框邊界：後面還有就是放不下
                return off;
            }
            const uint8_t b = buf[off];
            if (b != STX_V1 && b != STX_V2) {
                return off;     // 不是訊框開頭：留給下一個資料包
            }
            const uint16_t len = frame_len(buf + off, left, &known);
            if (!known) {
                *full = more;   // 標頭還沒寫完：被截斷就是放不下，否則等寫入端補齊
                return off;
            }
            if ((uint32_t)off + len > cap) {
                return off;     // 放不下
            }
            if (len > left) {
                *full = false;  // 放得下但還沒寫完：等
                return off;
            }
            off = (uint16_t)(off + len);
        }
    }

    // 有完整訊息可送時呼叫：true＝現在送，false＝再等
    bool ready(uint32_t now_ms, bool full)
    {
        if (!_waiting) {
            _waiting = true;
            _since_ms = now_ms;
        }
        if (full || _flushing) {
            return true;
        }
        if ((uint32_t)(now_ms - _since_ms) >= COALESCE_MS) {
            _flushing = true;
            return true;
        }
        return false;
    }

    // 緩衝區沒有完整訊框可送（空了，或開頭的訊框還沒寫完）：這一段忙碌期結束
    void idle()
    {
        _waiting = false;
        _flushing = false;
    }

    // socket 關閉時呼叫（斷線）
    void reset() { idle(); }

    bool waiting() const { return _waiting; }
    bool flushing() const { return _flushing; }

private:
    bool _waiting{false};
    bool _flushing{false};
    uint32_t _since_ms{0};
};

}
