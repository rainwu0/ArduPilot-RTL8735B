/* SPDX-License-Identifier: GPL-3.0-or-later */
// sdcard_shim.c 的主機測試：SDK 的 SD 主控函式表換成 RAM 上的假卡，記下每次呼叫的順序。
// 假卡不是硬體模型：驅動內部的行為（快取維護、讓出 CPU 的時機）看不到，這裡只驗受測程式的呼叫方式。
#include "sdcard_shim.h"
#include <hal_cache.h>
#include <hal_sdhost.h>
#include <task.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace {

constexpr uint32_t BLK = RTL8735B_SDCARD_BLOCK_SIZE;

struct Event {
    enum Kind { INIT_HOST, INIT_CARD, DEINIT, READ, WRITE, CLEAN, INVALIDATE, YIELD_HOOK, DELAY } kind;
    uintptr_t addr;   // 讀寫的緩衝區或快取操作的位址
    uint32_t a;       // 區塊位址、快取操作的長度或延遲的 tick 數
    uint32_t b;       // 區塊數
};

struct FakeCard {
    std::vector<Event> log;
    std::map<uint32_t, std::array<uint8_t, BLK>> written;
    uint32_t capacity = 4096;
    hal_status_t init_host_ret = HAL_OK;
    hal_status_t init_card_ret = HAL_OK;
    int fail_io_after = -1;   // 第幾次讀寫回傳錯誤（0 表示下一次）；-1 不出錯
    bool busy = false;        // 讀寫時呼叫一次 task_yield 回呼，模擬等卡忙碌
    uint8_t seen_force_33v = 0;
    bool seen_cache_cb = false;
    bool seen_yield_cb = false;
    uint32_t misaligned = 0;
    uint32_t out_of_range = 0;
    uint32_t delay_ticks = 0;
    hal_sdhost_adapter_t *adapter = nullptr;
};

FakeCard card;

void reset_card()
{
    rtl8735b_sdcard_deinit();
    card = FakeCard();
}

uint8_t pattern(uint32_t block, uint32_t i)
{
    return uint8_t(block * 7u + i * 13u + 1u);
}

void log_event(Event::Kind kind, uintptr_t addr, uint32_t a, uint32_t b)
{
    card.log.push_back(Event{kind, addr, a, b});
}

bool io_fails()
{
    if (card.fail_io_after < 0) {
        return false;
    }
    if (card.fail_io_after == 0) {
        card.fail_io_after = -1;
        return true;
    }
    card.fail_io_after--;
    return false;
}

void check_xfer(hal_sdhost_adapter_t *a, u32 blk, u16 cnt, const void *buf)
{
    if (((uintptr_t)buf & 31u) != 0) {
        card.misaligned++;
    }
    if (cnt == 0 || blk >= card.capacity || cnt > card.capacity - blk) {
        card.out_of_range++;
    }
    if (card.busy && a->task_yield_cb != nullptr) {
        a->task_yield_cb(a->task_yield_cb_para);
    }
}

hal_status_t fake_init_host(hal_sdhost_adapter_t *a)
{
    log_event(Event::INIT_HOST, 0, 0, 0);
    card.adapter = a;
    // 正式的 init_host 是否清掉可設定的欄位，SDK 沒有原始碼可查；替身清掉，確認受測程式在它之後才設定
    memset(a, 0, sizeof(*a));
    a->magic_num = 0x5D;
    return card.init_host_ret;
}

hal_status_t fake_init_card(hal_sdhost_adapter_t *a)
{
    log_event(Event::INIT_CARD, 0, 0, 0);
    card.seen_force_33v = a->force_33v;
    card.seen_cache_cb = a->dcache_invalidate_by_addr == dcache_invalidate_by_addr &&
                         a->dcache_clean_by_addr == dcache_clean_by_addr;
    card.seen_yield_cb = a->task_yield_cb != nullptr;
    if (card.init_card_ret != HAL_OK) {
        return card.init_card_ret;
    }
    a->blk_capacity = card.capacity;
    a->card_inited = 1;
    return HAL_OK;
}

void fake_deinit(hal_sdhost_adapter_t *a)
{
    log_event(Event::DEINIT, 0, 0, 0);
    a->card_inited = 0;
}

hal_status_t fake_read(hal_sdhost_adapter_t *a, u32 blk, u16 cnt, u8 *buf)
{
    log_event(Event::READ, (uintptr_t)buf, blk, cnt);
    check_xfer(a, blk, cnt, buf);
    if (io_fails()) {
        return HAL_ERR_HW;
    }
    for (uint32_t k = 0; k < cnt; k++) {
        const auto it = card.written.find(blk + k);
        for (uint32_t i = 0; i < BLK; i++) {
            buf[k * BLK + i] = it != card.written.end() ? it->second[i] : pattern(blk + k, i);
        }
    }
    return HAL_OK;
}

hal_status_t fake_write(hal_sdhost_adapter_t *a, u32 blk, u16 cnt, const u8 *buf)
{
    log_event(Event::WRITE, (uintptr_t)buf, blk, cnt);
    check_xfer(a, blk, cnt, buf);
    if (io_fails()) {
        return HAL_ERR_HW;
    }
    for (uint32_t k = 0; k < cnt; k++) {
        memcpy(card.written[blk + k].data(), buf + k * BLK, BLK);
    }
    return HAL_OK;
}

void fake_yield_hook(hal_sdhost_adapter_t *a, sdhost_para_cb_t cb, void *para)
{
    log_event(Event::YIELD_HOOK, 0, 0, 0);
    a->task_yield_cb = cb;
    a->task_yield_cb_para = para;
}

size_t count_kind(Event::Kind kind)
{
    size_t n = 0;
    for (const auto &e : card.log) {
        n += e.kind == kind;
    }
    return n;
}

// 第一個符合種類的事件的索引；沒有時回傳 log 長度
size_t find_kind(Event::Kind kind, size_t from = 0)
{
    for (size_t i = from; i < card.log.size(); i++) {
        if (card.log[i].kind == kind) {
            return i;
        }
    }
    return card.log.size();
}

uint8_t *aligned_buffer(uint32_t blocks)
{
    void *p = aligned_alloc(32, blocks * BLK);
    assert(p != nullptr);
    return static_cast<uint8_t *>(p);
}

void fill(uint8_t *buf, uint32_t blocks, uint8_t seed)
{
    for (uint32_t i = 0; i < blocks * BLK; i++) {
        buf[i] = uint8_t(seed + i * 31u);
    }
}

void test_init_order()
{
    reset_card();
    assert(rtl8735b_sdcard_ready() == 0);
    assert(rtl8735b_sdcard_block_count() == 0);
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    // 先初始化主控，再設定選項，最後識別卡
    const size_t host = find_kind(Event::INIT_HOST);
    const size_t hook = find_kind(Event::YIELD_HOOK);
    const size_t init = find_kind(Event::INIT_CARD);
    assert(host < hook && hook < init && init < card.log.size());
    assert(count_kind(Event::INIT_HOST) == 1 && count_kind(Event::INIT_CARD) == 1);
    assert(card.seen_force_33v == 1);
    assert(card.seen_cache_cb);
    assert(card.seen_yield_cb);
    assert(card.adapter->verbose == 0);
    assert(rtl8735b_sdcard_ready() == 1);
    assert(rtl8735b_sdcard_block_count() == card.capacity);

    // 讓出 CPU 的回呼真的讓出：至少一個 tick
    card.adapter->task_yield_cb(card.adapter->task_yield_cb_para);
    assert(card.delay_ticks >= 1);

    // 已初始化時再 init：先關掉主控再重來
    card.log.clear();
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    assert(find_kind(Event::DEINIT) < find_kind(Event::INIT_HOST));
    assert(find_kind(Event::INIT_HOST) < card.log.size());
}

void test_init_failures()
{
    reset_card();
    card.init_host_ret = HAL_ERR_HW;
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_EHOST);
    assert(count_kind(Event::INIT_CARD) == 0);
    assert(count_kind(Event::DEINIT) == 0);
    assert(rtl8735b_sdcard_ready() == 0);
    assert(rtl8735b_sdcard_block_count() == 0);

    // 識別失敗（沒插卡）：關掉主控，讓下一次 init 從頭開始
    reset_card();
    card.init_card_ret = HAL_TIMEOUT;
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_ECARD);
    assert(find_kind(Event::INIT_CARD) < find_kind(Event::DEINIT));
    assert(find_kind(Event::DEINIT) < card.log.size());
    assert(rtl8735b_sdcard_ready() == 0);
    assert(rtl8735b_sdcard_block_count() == 0);

    // 容量為 0 的卡不能用
    reset_card();
    card.capacity = 0;
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_ECARD);
    assert(count_kind(Event::DEINIT) == 1);
    assert(rtl8735b_sdcard_ready() == 0);

    // 失敗之後可以再 init
    card.capacity = 4096;
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    assert(rtl8735b_sdcard_ready() == 1);
}

void test_not_ready()
{
    reset_card();
    uint8_t *buf = aligned_buffer(1);
    assert(rtl8735b_sdcard_read(0, buf, 1) == RTL8735B_SDCARD_ENOCARD);
    assert(rtl8735b_sdcard_write(0, buf, 1) == RTL8735B_SDCARD_ENOCARD);
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    rtl8735b_sdcard_deinit();
    assert(count_kind(Event::DEINIT) == 1);
    assert(rtl8735b_sdcard_ready() == 0);
    assert(rtl8735b_sdcard_block_count() == 0);
    assert(rtl8735b_sdcard_read(0, buf, 1) == RTL8735B_SDCARD_ENOCARD);
    assert(rtl8735b_sdcard_write(0, buf, 1) == RTL8735B_SDCARD_ENOCARD);
    assert(count_kind(Event::READ) == 0 && count_kind(Event::WRITE) == 0);
    // 沒有初始化時 deinit 不碰主控
    rtl8735b_sdcard_deinit();
    assert(count_kind(Event::DEINIT) == 1);
    free(buf);
}

void test_args()
{
    reset_card();
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    uint8_t *buf = aligned_buffer(2);
    assert(rtl8735b_sdcard_read(0, nullptr, 1) == RTL8735B_SDCARD_EINVAL);
    assert(rtl8735b_sdcard_write(0, nullptr, 1) == RTL8735B_SDCARD_EINVAL);
    assert(rtl8735b_sdcard_read(card.capacity, buf, 1) == RTL8735B_SDCARD_EINVAL);
    assert(rtl8735b_sdcard_write(card.capacity - 1, buf, 2) == RTL8735B_SDCARD_EINVAL);
    // 位址加長度溢位不能繞回卡的開頭
    assert(rtl8735b_sdcard_read(0xFFFFFFFFu, buf, 2) == RTL8735B_SDCARD_EINVAL);
    assert(rtl8735b_sdcard_write(0xFFFFFFFFu, buf, 2) == RTL8735B_SDCARD_EINVAL);
    assert(count_kind(Event::READ) == 0 && count_kind(Event::WRITE) == 0);
    // 參數錯誤不算讀寫失敗
    assert(rtl8735b_sdcard_ready() == 1);
    // 0 個區塊不做事
    assert(rtl8735b_sdcard_read(5, buf, 0) == RTL8735B_SDCARD_OK);
    assert(rtl8735b_sdcard_write(5, buf, 0) == RTL8735B_SDCARD_OK);
    assert(count_kind(Event::READ) == 0 && count_kind(Event::WRITE) == 0);
    // 最後一個區塊可以讀寫
    assert(rtl8735b_sdcard_write(card.capacity - 1, buf, 1) == RTL8735B_SDCARD_OK);
    assert(rtl8735b_sdcard_read(card.capacity - 1, buf, 1) == RTL8735B_SDCARD_OK);
    assert(card.out_of_range == 0);
    free(buf);
}

void test_aligned_direct()
{
    reset_card();
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    uint8_t *wbuf = aligned_buffer(3);
    uint8_t *rbuf = aligned_buffer(3);
    fill(wbuf, 3, 0x40);
    card.log.clear();
    assert(rtl8735b_sdcard_write(10, wbuf, 3) == RTL8735B_SDCARD_OK);
    // 對齊的緩衝區直接交給驅動；寫之前把快取寫回
    const size_t w = find_kind(Event::WRITE);
    assert(w < card.log.size() && count_kind(Event::WRITE) == 1);
    assert(card.log[w].addr == (uintptr_t)wbuf && card.log[w].a == 10 && card.log[w].b == 3);
    const size_t c = find_kind(Event::CLEAN);
    assert(c < w && card.log[c].addr == (uintptr_t)wbuf && card.log[c].a == 3 * BLK);

    card.log.clear();
    assert(rtl8735b_sdcard_read(10, rbuf, 3) == RTL8735B_SDCARD_OK);
    const size_t r = find_kind(Event::READ);
    assert(r < card.log.size() && count_kind(Event::READ) == 1);
    assert(card.log[r].addr == (uintptr_t)rbuf && card.log[r].a == 10 && card.log[r].b == 3);
    // 讀完作廢快取，CPU 才看得到 DMA 寫進記憶體的內容
    const size_t inv = find_kind(Event::INVALIDATE, r);
    assert(inv < card.log.size() && card.log[inv].addr == (uintptr_t)rbuf && card.log[inv].a == 3 * BLK);
    assert(memcmp(wbuf, rbuf, 3 * BLK) == 0);
    assert(card.misaligned == 0 && card.out_of_range == 0);
    free(wbuf);
    free(rbuf);
}

void test_unaligned_bounce()
{
    reset_card();
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    const uint32_t n = 20;
    uint8_t *wraw = aligned_buffer(n + 1);
    uint8_t *rraw = aligned_buffer(n + 1);
    uint8_t *wbuf = wraw + 1;
    uint8_t *rbuf = rraw + 3;
    fill(wbuf, n, 0x11);
    card.log.clear();
    assert(rtl8735b_sdcard_write(100, wbuf, n) == RTL8735B_SDCARD_OK);
    // 未對齊的緩衝區經對齊的暫存分段寫，區塊位址連續
    uint32_t next = 100, total = 0;
    for (const auto &e : card.log) {
        if (e.kind != Event::WRITE) {
            continue;
        }
        assert(e.addr != (uintptr_t)wbuf);
        assert(e.a == next);
        next += e.b;
        total += e.b;
    }
    assert(total == n && count_kind(Event::WRITE) > 1);
    assert(rtl8735b_sdcard_read(100, rbuf, n) == RTL8735B_SDCARD_OK);
    assert(memcmp(wbuf, rbuf, n * BLK) == 0);
    assert(card.misaligned == 0 && card.out_of_range == 0);
    // 讀到未寫過的區塊：內容照假卡的樣式
    assert(rtl8735b_sdcard_read(3000, rbuf, 2) == RTL8735B_SDCARD_OK);
    assert(rbuf[0] == pattern(3000, 0) && rbuf[BLK + 5] == pattern(3001, 5));
    free(wraw);
    free(rraw);
}

void test_large_transfer()
{
    // 驅動一次最多 65535 個區塊（blk_cnt 是 u16）
    reset_card();
    card.capacity = 70000;
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    const uint32_t n = 65537;
    uint8_t *buf = aligned_buffer(n);
    card.log.clear();
    assert(rtl8735b_sdcard_read(1000, buf, n) == RTL8735B_SDCARD_OK);
    std::vector<Event> reads;
    for (const auto &e : card.log) {
        if (e.kind == Event::READ) {
            reads.push_back(e);
        }
    }
    assert(reads.size() == 2);
    assert(reads[0].a == 1000 && reads[0].b == 65535 && reads[0].addr == (uintptr_t)buf);
    assert(reads[1].a == 1000 + 65535 && reads[1].b == 2 && reads[1].addr == (uintptr_t)(buf + 65535u * BLK));
    assert(buf[0] == pattern(1000, 0));
    assert(buf[(n - 1) * BLK + 7] == pattern(1000 + n - 1, 7));
    assert(card.out_of_range == 0);

    card.log.clear();
    assert(rtl8735b_sdcard_write(1000, buf, n) == RTL8735B_SDCARD_OK);
    assert(count_kind(Event::WRITE) == 2);
    free(buf);
}

void test_io_error()
{
    reset_card();
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    uint8_t *buf = aligned_buffer(1);
    card.fail_io_after = 0;
    assert(rtl8735b_sdcard_write(0, buf, 1) == RTL8735B_SDCARD_EIO);
    // 失敗不改變初始化的狀態：下一次讀寫照常交給驅動（要不要重來由檔案系統後端決定；
    // 若變成未就緒，FatFs 之後只會回報 FR_NOT_READY，後端只在 FR_DISK_ERR 時重掛，就永遠掛不回來）
    assert(rtl8735b_sdcard_ready() == 1);
    assert(rtl8735b_sdcard_block_count() == card.capacity);
    assert(count_kind(Event::DEINIT) == 0);
    card.log.clear();
    assert(rtl8735b_sdcard_read(0, buf, 1) == RTL8735B_SDCARD_OK);
    assert(count_kind(Event::READ) == 1);

    // 分段傳輸的第二段失敗：回報失敗，不再傳後面的段
    uint8_t *raw = aligned_buffer(30);
    card.fail_io_after = 1;
    card.log.clear();
    assert(rtl8735b_sdcard_read(0, raw + 1, 25) == RTL8735B_SDCARD_EIO);
    assert(count_kind(Event::READ) == 2);
    card.fail_io_after = 1;
    card.log.clear();
    assert(rtl8735b_sdcard_write(0, raw + 1, 25) == RTL8735B_SDCARD_EIO);
    assert(count_kind(Event::WRITE) == 2);
    free(buf);
    free(raw);
}

void test_yield_while_busy()
{
    reset_card();
    assert(rtl8735b_sdcard_init() == RTL8735B_SDCARD_OK);
    uint8_t *buf = aligned_buffer(1);
    card.busy = true;
    card.delay_ticks = 0;
    card.log.clear();
    assert(rtl8735b_sdcard_write(1, buf, 1) == RTL8735B_SDCARD_OK);
    const size_t w = find_kind(Event::WRITE);
    const size_t d = find_kind(Event::DELAY, w);
    assert(w < card.log.size() && d < card.log.size());
    assert(card.delay_ticks >= 1);
    free(buf);
}

}  // namespace

extern "C" {
const hal_sdhost_func_stubs_t hal_sdhost_stubs = {
    fake_init_host,
    fake_init_card,
    fake_deinit,
    fake_read,
    fake_write,
    fake_yield_hook,
};

void dcache_invalidate_by_addr(uint32_t *addr, int32_t dsize)
{
    log_event(Event::INVALIDATE, (uintptr_t)addr, (uint32_t)dsize, 0);
}

void dcache_clean_by_addr(uint32_t *addr, int32_t dsize)
{
    log_event(Event::CLEAN, (uintptr_t)addr, (uint32_t)dsize, 0);
}

void vTaskDelay(TickType_t ticks)
{
    log_event(Event::DELAY, 0, ticks, 0);
    card.delay_ticks += ticks;
}
}

int main()
{
    test_init_order();
    test_init_failures();
    test_not_ready();
    test_args();
    test_aligned_direct();
    test_unaligned_bounce();
    test_large_transfer();
    test_io_error();
    test_yield_while_busy();
    printf("sdcard shim: all tests passed\n");
    return 0;
}
