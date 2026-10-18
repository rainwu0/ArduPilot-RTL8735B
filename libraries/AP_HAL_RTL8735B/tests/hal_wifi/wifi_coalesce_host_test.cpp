/* WiFiCoalesce 的主機測試：訊框長度、往後接完整訊框、資料包上限、非 MAVLink 資料、等待與送出的時機。
 * 不依賴 SDK 與 FreeRTOS。 */

#include <AP_HAL_RTL8735B/WiFiCoalesce.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

using RTL8735B::WiFiCoalesce;

namespace {

constexpr uint16_t CAP = 1472;

// 在 buf+off 放一則 MAVLink 2 訊框（酬載 payload 位元組，signed 時加 13 bytes 簽章），回傳總長
uint16_t put_v2(uint8_t *buf, uint16_t off, uint8_t payload, bool signed_frame = false)
{
    const uint16_t len = (uint16_t)(payload + 12 + (signed_frame ? 13 : 0));
    memset(buf + off, 0x55, len);
    buf[off] = 0xFD;
    buf[off + 1] = payload;
    buf[off + 2] = signed_frame ? 0x01 : 0x00;
    return len;
}

uint16_t put_v1(uint8_t *buf, uint16_t off, uint8_t payload)
{
    const uint16_t len = (uint16_t)(payload + 8);
    memset(buf + off, 0x55, len);
    buf[off] = 0xFE;
    buf[off + 1] = payload;
    return len;
}

void test_frame_len()
{
    uint8_t b[64] = {0};
    bool known;
    assert(WiFiCoalesce::frame_len(b, 0, &known) == 0 && !known);
    put_v2(b, 0, 52);
    assert(WiFiCoalesce::frame_len(b, 3, &known) == 64 && known);
    assert(WiFiCoalesce::frame_len(b, 2, &known) == 0 && !known);   // 還看不到 incompat_flags
    put_v2(b, 0, 10, true);
    assert(WiFiCoalesce::frame_len(b, 3, &known) == 35 && known);   // 10 + 12 + 13
    put_v1(b, 0, 9);
    assert(WiFiCoalesce::frame_len(b, 2, &known) == 17 && known);
    b[0] = 0x00;
    assert(WiFiCoalesce::frame_len(b, 10, &known) == 0 && !known);
}

void test_extend_joins_whole_frames()
{
    uint8_t b[CAP];
    uint16_t n = 0;
    const uint16_t first = put_v2(b, n, 63);    // 75 bytes，約一則 HIGHRES_IMU
    n += first;
    n += put_v2(b, n, 63);
    n += put_v1(b, n, 9);
    n += put_v2(b, n, 20, true);
    bool full;
    // 全部都完整、後面沒有了：全部接上，還可以等
    assert(WiFiCoalesce::extend(b, n, first, false, CAP, &full) == n);
    assert(!full);
    // n 正好在訊框邊界、緩衝區還有更多：全部接上，現在送
    assert(WiFiCoalesce::extend(b, n, first, true, CAP, &full) == n);
    assert(full);
}

void test_extend_stops_at_partial_frame()
{
    uint8_t b[CAP];
    uint16_t n = 0;
    const uint16_t first = put_v2(b, n, 63);
    n += first;
    const uint16_t second = put_v2(b, n, 63);
    bool full;
    // 第二則只寫了一半：放得下但還沒寫完，等
    assert(WiFiCoalesce::extend(b, (uint16_t)(n + second / 2), first, false, CAP, &full) == first);
    assert(!full);
    // 第二則只寫了 2 個位元組（看不到旗標）：還沒被截斷就等
    assert(WiFiCoalesce::extend(b, (uint16_t)(n + 2), first, false, CAP, &full) == first);
    assert(!full);
}

void test_extend_respects_cap()
{
    uint8_t b[CAP];
    uint16_t n = 0;
    const uint16_t first = put_v2(b, n, 63);
    n += first;
    // 一直接 75 bytes 的訊框，接到再一則就超過 1,472
    while (n + 75 <= CAP) {
        n += put_v2(b, n, 63);
    }
    const uint16_t fit = n;
    assert(fit == 75 * 19);
    // 偷看到的位元組被 cap 截斷在下一則中間：放不下，現在送
    b[fit] = 0xFD;
    b[fit + 1] = 63;
    b[fit + 2] = 0;
    bool full;
    assert(WiFiCoalesce::extend(b, CAP, first, true, CAP, &full) == fit);
    assert(full);
    // 下一則標頭完整但總長超過 cap（即使緩衝區只寫到這裡）：放不下，現在送
    assert(WiFiCoalesce::extend(b, (uint16_t)(fit + 3), first, false, CAP, &full) == fit);
    assert(full);
    // 截斷在只剩 2 個位元組的標頭：被截斷，現在送
    assert(WiFiCoalesce::extend(b, (uint16_t)(fit + 2), first, true, CAP, &full) == fit);
    assert(full);
}

void test_extend_non_mavlink()
{
    uint8_t b[CAP];
    memset(b, 0x41, sizeof b);
    bool full;
    // 開頭不是 MAVLink：mavlink_packetise 給的那一段單獨送
    assert(WiFiCoalesce::extend(b, 200, 200, false, CAP, &full) == 200);
    assert(full);
    // 一則 MAVLink 之後跟著非 MAVLink：只送那一則，後面留給下一個資料包
    const uint16_t first = put_v2(b, 0, 63);
    assert(WiFiCoalesce::extend(b, 200, first, false, CAP, &full) == first);
    assert(full);
    // first 異常：原樣回傳
    assert(WiFiCoalesce::extend(b, 10, 0, false, CAP, &full) == 0);
    assert(WiFiCoalesce::extend(b, 10, 20, false, CAP, &full) == 20);
}

void test_ready_waits_then_flushes()
{
    WiFiCoalesce c;
    // 湊不滿：從第一次看到起等 COALESCE_MS
    assert(!c.ready(1000, false));
    assert(c.waiting() && !c.flushing());
    for (uint32_t t = 1; t < WiFiCoalesce::COALESCE_MS; t++) {
        assert(!c.ready(1000 + t, false));
    }
    assert(c.ready(1000 + WiFiCoalesce::COALESCE_MS, false));
    assert(c.flushing());
    // 送出之後同一段忙碌期繼續送，不再等
    assert(c.ready(1000 + WiFiCoalesce::COALESCE_MS, false));
    assert(c.ready(1000 + WiFiCoalesce::COALESCE_MS + 1, false));
    // 緩衝區空了：下一段重新計時
    c.idle();
    assert(!c.waiting() && !c.flushing());
    assert(!c.ready(2000, false));
    assert(c.ready(2000 + WiFiCoalesce::COALESCE_MS, false));
}

void test_ready_full_sends_and_keeps_timer()
{
    WiFiCoalesce c;
    // 湊滿就送，不必等
    assert(c.ready(1000, true));
    // 湊滿送出後剩下的：照同一段的計時，不從頭算（每則最多多等 COALESCE_MS）
    assert(!c.ready(1005, false));
    assert(c.ready(1000 + WiFiCoalesce::COALESCE_MS, false));
}

void test_ready_millis_wrap_and_reset()
{
    WiFiCoalesce c;
    const uint32_t start = 0xFFFFFFFCu;
    assert(!c.ready(start, false));
    assert(!c.ready(start + WiFiCoalesce::COALESCE_MS - 1, false));
    assert(c.ready(start + WiFiCoalesce::COALESCE_MS, false));
    c.reset();
    assert(!c.waiting() && !c.flushing());
}

}  // namespace

int main()
{
    test_frame_len();
    test_extend_joins_whole_frames();
    test_extend_stops_at_partial_frame();
    test_extend_respects_cap();
    test_extend_non_mavlink();
    test_ready_waits_then_flushes();
    test_ready_full_sends_and_keeps_timer();
    test_ready_millis_wrap_and_reset();
    printf("wifi_coalesce host test: PASS\n");
    return 0;
}
