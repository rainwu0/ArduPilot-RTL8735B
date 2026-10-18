/* WiFiTxRetry 的主機測試：緩衝不足時留著重試、時間與次數上限、其他錯誤直接丟、世代改變與斷線放棄重試。
 * 不依賴 SDK 與 FreeRTOS。 */

#include <AP_HAL_RTL8735B/WiFiTxRetry.h>

#include <assert.h>
#include <stdio.h>

using RTL8735B::WiFiTxRetry;
using Action = WiFiTxRetry::Action;

namespace {

void test_limits()
{
    // 500 ms 涵蓋實測上驅動拒絕送出的大部分時間；APM_WIFI task 每步約 1 ms，次數上限跟著設 500
    assert(WiFiTxRetry::RETRY_MS == 500);
    assert(WiFiTxRetry::RETRY_MAX == 500);
}

void test_sent_without_failure()
{
    WiFiTxRetry r;
    assert(r.on_result(true, false, 100, 1) == Action::SENT);
    assert(!r.active());
}

void test_retry_then_sent()
{
    WiFiTxRetry r;
    assert(r.on_result(false, true, 100, 1) == Action::KEEP);
    assert(r.active());
    assert(r.on_result(false, true, 101, 1) == Action::KEEP);
    assert(r.on_result(true, false, 102, 1) == Action::SENT_RETRIED);
    assert(!r.active());
    // 下一個資料包從頭算
    assert(r.on_result(true, false, 103, 1) == Action::SENT);
}

void test_time_limit()
{
    WiFiTxRetry r;
    // 每 1 ms 一步：第 0 到 RETRY_MS−1 ms 都留著，第 RETRY_MS ms 丟掉
    for (uint32_t t = 0; t < WiFiTxRetry::RETRY_MS; t++) {
        assert(r.on_result(false, true, 1000 + t, 7) == Action::KEEP);
    }
    assert(r.on_result(false, true, 1000 + WiFiTxRetry::RETRY_MS, 7) == Action::DROP);
    assert(!r.active());
    // 丟掉之後，下一個資料包重新開始
    assert(r.on_result(false, true, 1000 + WiFiTxRetry::RETRY_MS + 1, 7) == Action::KEEP);
}

void test_count_limit()
{
    WiFiTxRetry r;
    // 同一毫秒內連續失敗（步驟比 1 ms 快時）：第 RETRY_MAX 次仍留著，下一次丟掉
    for (uint16_t i = 0; i < WiFiTxRetry::RETRY_MAX; i++) {
        assert(r.on_result(false, true, 500, 3) == Action::KEEP);
    }
    assert(r.on_result(false, true, 500, 3) == Action::DROP);
    assert(!r.active());
}

void test_other_error_drops_at_once()
{
    WiFiTxRetry r;
    assert(r.on_result(false, false, 10, 1) == Action::DROP);
    assert(!r.active());
    // 重試中遇到其他錯誤也直接丟掉
    assert(r.on_result(false, true, 11, 1) == Action::KEEP);
    assert(r.on_result(false, false, 12, 1) == Action::DROP);
    assert(!r.active());
}

void test_generation_change()
{
    WiFiTxRetry r;
    assert(r.on_result(false, true, 100, 1) == Action::KEEP);
    // 緩衝區清空過：放棄重試中的資料包
    r.check_generation(2);
    assert(!r.active());
    // 新世代的資料包從 105 ms 重新計時
    assert(r.on_result(false, true, 105, 2) == Action::KEEP);
    assert(r.on_result(false, true, 105 + WiFiTxRetry::RETRY_MS - 1, 2) == Action::KEEP);
    assert(r.on_result(false, true, 105 + WiFiTxRetry::RETRY_MS, 2) == Action::DROP);
    // 同一世代時 check_generation 不影響重試
    assert(r.on_result(false, true, 1000, 2) == Action::KEEP);
    r.check_generation(2);
    assert(r.active());
}

void test_generation_mismatch_in_result()
{
    WiFiTxRetry r;
    assert(r.on_result(false, true, 100, 1) == Action::KEEP);
    // 沒呼叫 check_generation、世代卻不同：當成新的資料包，從頭計時
    assert(r.on_result(false, true, 130, 2) == Action::KEEP);
    assert(r.on_result(false, true, 130 + WiFiTxRetry::RETRY_MS - 1, 2) == Action::KEEP);
    assert(r.on_result(false, true, 130 + WiFiTxRetry::RETRY_MS, 2) == Action::DROP);
}

void test_reset()
{
    WiFiTxRetry r;
    assert(r.on_result(false, true, 100, 1) == Action::KEEP);
    r.reset();
    assert(!r.active());
    // reset 之後送出成功不算重試成功
    assert(r.on_result(true, false, 101, 1) == Action::SENT);
}

void test_millis_wrap()
{
    WiFiTxRetry r;
    const uint32_t start = 0xFFFFFFF0u;
    assert(r.on_result(false, true, start, 1) == Action::KEEP);
    assert(r.on_result(false, true, start + WiFiTxRetry::RETRY_MS - 1u, 1) == Action::KEEP);   // 繞回 0 之後
    assert(r.on_result(false, true, start + WiFiTxRetry::RETRY_MS, 1) == Action::DROP);
}

}  // namespace

int main()
{
    test_limits();
    test_sent_without_failure();
    test_retry_then_sent();
    test_time_limit();
    test_count_limit();
    test_other_error_drops_at_once();
    test_generation_change();
    test_generation_mismatch_in_result();
    test_reset();
    test_millis_wrap();
    printf("wifi_tx_retry host test: PASS\n");
    return 0;
}
