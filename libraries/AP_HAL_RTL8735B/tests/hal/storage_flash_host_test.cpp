/* SPDX-License-Identifier: GPL-3.0-or-later */
// Storage 以 AP_FlashStorage 持久化到外部 NOR 的主機測試：假 Flash 見 storage_flash_fake.cpp。
#include "Storage.h"
#include "storage_shim.h"
#include "storage_fakes/sdk/storage_flash_fake.h"
#include <AP_Math/AP_Math.h>
#include <AP_InternalError/AP_InternalError.h>

#include <assert.h>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <vector>

static uint32_t now_ms = 1;
static AP_HAL::Util util_instance;
static AP_HAL::HAL hal_instance{&util_instance};
const AP_HAL::HAL &hal = hal_instance;

namespace AP_HAL {
uint32_t millis() { return now_ms; }
void panic(const char *, ...) { throw std::runtime_error("panic"); }
}

// AP_HAL/Storage.cpp 的預設 erase()（寫入全零）；真實檔案會引入整個 AP_HAL，這裡照抄其行為。
bool AP_HAL::Storage::erase()
{
    uint8_t blk[16] {};
    for (uint32_t ofs = 0; ofs < HAL_STORAGE_SIZE; ofs += sizeof(blk)) {
        const uint32_t n = MIN(uint32_t(sizeof(blk)), HAL_STORAGE_SIZE - ofs);
        write_block(ofs, blk, n);
    }
    return true;
}

using StoragePtr = std::unique_ptr<RTL8735B::Storage>;
static StorageFlashFake &flash = storage_flash_fake;

static StoragePtr boot()
{
    StoragePtr s(new RTL8735B::Storage());
    s->init();
    return s;
}

// storage task 每 tick 寫一行；推進時間讓 healthy() 看得到「最近 2 秒內曾經沒有待寫資料」
static void run_ticks(RTL8735B::Storage &s, unsigned ticks)
{
    for (unsigned i = 0; i < ticks; i++) {
        s._timer_tick();
        now_ms++;
    }
}

static void flush(RTL8735B::Storage &s)
{
    run_ticks(s, HAL_STORAGE_SIZE / 8 + 16);
}

static std::vector<uint8_t> read_all(RTL8735B::Storage &s)
{
    std::vector<uint8_t> out(HAL_STORAGE_SIZE);
    s.read_block(out.data(), 0, out.size());
    return out;
}

static void check_region_only()
{
    assert(AP_InternalError::recursion_errors == 0);
    assert(flash.unlocked_ops == 0);
    assert(flash.unaligned_cache_ops == 0);
    if (flash.max_touched != 0) {
        assert(flash.min_touched >= RTL8735B_STORAGE_FLASH_BASE);
        assert(flash.max_touched < RTL8735B_STORAGE_FLASH_BASE + RTL8735B_STORAGE_FLASH_SIZE);
    }
}

static void region_layout()
{
    // AP_FlashStorage 需要兩個邏輯扇區，每個要放得下一次完整寫出（reserve_size）再加扇區標頭
    static_assert(RTL8735B_STORAGE_FLASH_SECTOR_COUNT == 2, "AP_FlashStorage uses two sectors");
    static_assert(RTL8735B_STORAGE_FLASH_SECTOR_SIZE % RTL8735B_STORAGE_FLASH_ERASE_SIZE == 0,
                  "logical sector must be whole NOR erase sectors");
    static_assert(RTL8735B_STORAGE_FLASH_BASE % RTL8735B_STORAGE_FLASH_ERASE_SIZE == 0,
                  "region must be erase aligned");
    static_assert(RTL8735B_STORAGE_FLASH_SECTOR_SIZE >= 2 * HAL_STORAGE_SIZE + 1024,
                  "sector too small for full write-out");
    // 選定區間：SDK 分割表 mp（0xFC0000）之前、使用者資料（0xF00000–0xF64000）之後，映像（到 0x42E000）之外
    static_assert(RTL8735B_STORAGE_FLASH_BASE >= 0xF64000, "overlaps SDK user data");
    static_assert(RTL8735B_STORAGE_FLASH_BASE + RTL8735B_STORAGE_FLASH_SIZE <= 0xFC0000, "overlaps mp partition");
}

static void shim_contract()
{
    flash.reset();
    uint8_t id[3]{};
    assert(rtl8735b_storage_flash_init(id) == RTL8735B_STORAGE_FLASH_OK);
    assert(id[0] == 0xEF && id[2] == 0x18);

    uint8_t data[300];
    for (unsigned i = 0; i < sizeof(data); i++) { data[i] = uint8_t(i * 7 + 1); }
    const uint32_t end = RTL8735B_STORAGE_FLASH_SIZE;
    // 區間外、溢位、未對齊抹除一律拒絕，不碰 Flash
    assert(rtl8735b_storage_flash_read(end - 1, data, 2) == RTL8735B_STORAGE_FLASH_EINVAL);
    assert(rtl8735b_storage_flash_program(end, data, 1) == RTL8735B_STORAGE_FLASH_EINVAL);
    assert(rtl8735b_storage_flash_program(0xFFFFFFFFu, data, 2) == RTL8735B_STORAGE_FLASH_EINVAL);
    assert(rtl8735b_storage_flash_erase(0x800, 0x1000) == RTL8735B_STORAGE_FLASH_EINVAL);
    assert(rtl8735b_storage_flash_erase(0, 0x800) == RTL8735B_STORAGE_FLASH_EINVAL);
    assert(rtl8735b_storage_flash_erase(end - 0x1000, 0x2000) == RTL8735B_STORAGE_FLASH_EINVAL);
    assert(rtl8735b_storage_flash_read(0, nullptr, 4) == RTL8735B_STORAGE_FLASH_EINVAL);
    assert(flash.program_calls == 0 && flash.erase_calls == 0 && flash.read_calls == 0);

    // 跨 256 位元組頁寫入、讀回
    assert(rtl8735b_storage_flash_program(0x1F0, data, sizeof(data)) == RTL8735B_STORAGE_FLASH_OK);
    uint8_t back[sizeof(data)]{};
    assert(rtl8735b_storage_flash_read(0x1F0, back, sizeof(back)) == RTL8735B_STORAGE_FLASH_OK);
    assert(memcmp(back, data, sizeof(data)) == 0);
    assert(flash.mem[RTL8735B_STORAGE_FLASH_BASE + 0x1F0] == data[0]);

    // NOR 只能清位元：寫入需要 0→1 的內容時，讀回比對失敗而不是回報成功
    const uint8_t ones[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    assert(rtl8735b_storage_flash_program(0x1F0, ones, sizeof(ones)) == RTL8735B_STORAGE_FLASH_EVERIFY);

    // 抹除後全為 0xFF；抹除不掉（壞區）時回報失敗
    assert(rtl8735b_storage_flash_erase(0, 0x2000) == RTL8735B_STORAGE_FLASH_OK);
    assert(rtl8735b_storage_flash_read(0x1F0, back, sizeof(back)) == RTL8735B_STORAGE_FLASH_OK);
    for (uint8_t b : back) { assert(b == 0xFF); }
    assert(rtl8735b_storage_flash_program(0x10, data, 16) == RTL8735B_STORAGE_FLASH_OK);
    flash.stuck_begin = RTL8735B_STORAGE_FLASH_BASE + 0x10;
    flash.stuck_end = flash.stuck_begin + 1;
    assert(rtl8735b_storage_flash_erase(0, 0x1000) == RTL8735B_STORAGE_FLASH_EVERIFY);
    flash.stuck_begin = flash.stuck_end = 0;

    // 容量不足或讀不到 ID：不可使用
    flash.id[2] = 0x17;
    assert(rtl8735b_storage_flash_init(id) == RTL8735B_STORAGE_FLASH_EID);
    flash.id[2] = 0x18;
    flash.id_valid = false;
    assert(rtl8735b_storage_flash_init(id) == RTL8735B_STORAGE_FLASH_EID);
    check_region_only();
}

static void first_boot_and_persist()
{
    flash.reset();
    now_ms = 1;
    StoragePtr s = boot();
    std::vector<uint8_t> zeros(HAL_STORAGE_SIZE, 0);
    assert(read_all(*s) == zeros);
    flush(*s);
    assert(s->healthy());
    // 第一次開機要把兩個邏輯扇區都抹過並寫上標頭
    assert(flash.erase_calls >= 2 * RTL8735B_STORAGE_FLASH_SECTOR_SIZE / RTL8735B_STORAGE_FLASH_ERASE_SIZE);

    const char hello[] = "hello flash";
    s->write_block(0, hello, sizeof(hello));
    const uint8_t tail[4] = {1, 2, 3, 4};
    s->write_block(HAL_STORAGE_SIZE - 4, tail, sizeof(tail));
    uint8_t pattern[300];
    for (unsigned i = 0; i < sizeof(pattern); i++) { pattern[i] = uint8_t(255 - i); }
    s->write_block(1003, pattern, sizeof(pattern));
    std::vector<uint8_t> expected = read_all(*s);

    // 還沒寫出時不能宣稱健康
    now_ms += 2500;
    assert(!s->healthy());
    flush(*s);
    assert(s->healthy());

    void *ptr = nullptr;
    size_t size = 0;
    assert(s->get_storage_ptr(ptr, size) && size == HAL_STORAGE_SIZE && ptr != nullptr);

    s.reset();
    StoragePtr rebooted = boot();
    assert(read_all(*rebooted) == expected);
    char back[sizeof(hello)]{};
    rebooted->read_block(back, 0, sizeof(back));
    assert(strcmp(back, hello) == 0);
    check_region_only();
}

static void write_random(RTL8735B::Storage &s, std::vector<uint8_t> &shadow, std::mt19937 &rng)
{
    const uint16_t len = uint16_t(1 + rng() % 24);
    const uint16_t ofs = uint16_t(rng() % (HAL_STORAGE_SIZE - len));
    uint8_t data[24];
    for (uint16_t i = 0; i < len; i++) { data[i] = uint8_t(rng()); }
    s.write_block(ofs, data, len);
    memcpy(&shadow[ofs], data, len);
}

static void sector_rotation_and_arming()
{
    flash.reset();
    now_ms = 1;
    std::mt19937 rng(1234);
    std::vector<uint8_t> shadow(HAL_STORAGE_SIZE, 0);
    StoragePtr s = boot();
    const unsigned first_boot_erases = flash.erase_calls;
    // 足以把兩個 64 KiB 邏輯扇區各寫滿數次
    for (unsigned n = 0; n < 30000; n++) {
        write_random(*s, shadow, rng);
        run_ticks(*s, 4);
    }
    flush(*s);
    assert(s->healthy());
    const unsigned rotated_erases = flash.erase_calls - first_boot_erases;
    assert(rotated_erases >= 2 * RTL8735B_STORAGE_FLASH_SECTOR_SIZE / RTL8735B_STORAGE_FLASH_ERASE_SIZE);
    assert(read_all(*s) == shadow);

    // 解鎖（armed）時不得抹除：扇區用完後資料留在待寫、healthy 轉為 false
    util_instance.soft_armed = true;
    const unsigned armed_erases = flash.erase_calls;
    bool went_unhealthy = false;
    for (unsigned n = 0; n < 30000 && !went_unhealthy; n++) {
        write_random(*s, shadow, rng);
        run_ticks(*s, 4);
        went_unhealthy = !s->healthy();
    }
    assert(went_unhealthy);
    assert(flash.erase_calls == armed_erases);
    // 上鎖後可以換扇區，寫完即恢復健康
    util_instance.soft_armed = false;
    flush(*s);
    flush(*s);
    assert(s->healthy());
    assert(flash.erase_calls > armed_erases);

    s.reset();
    StoragePtr rebooted = boot();
    assert(read_all(*rebooted) == shadow);
    check_region_only();
}

// 在一連串寫入的任意位元組處斷電：重開機後，已寫出的資料不變，斷電當下那筆寫入的每一行是舊值或新值
static void power_loss_sweep()
{
    const unsigned writes = 16000;
    struct Step { uint16_t ofs, len; uint8_t data[24]; };
    std::vector<Step> steps(writes);
    std::mt19937 rng(99);
    for (auto &st : steps) {
        st.len = uint16_t(1 + rng() % 24);
        st.ofs = uint16_t(rng() % (HAL_STORAGE_SIZE - st.len));
        for (uint16_t i = 0; i < st.len; i++) { st.data[i] = uint8_t(rng()); }
    }

    // 先跑一次不斷電的版本，找出總寫入量與換扇區（抹除）發生的位置
    flash.reset();
    now_ms = 1;
    unsigned long first_erase_bytes = 0;
    {
        StoragePtr s = boot();
        const unsigned base_erases = flash.erase_calls;
        for (auto &st : steps) {
            s->write_block(st.ofs, st.data, st.len);
            // 一筆最多 24 位元組、跨 4 行；每個 tick 寫出一行（換扇區也在同一個 tick 內完成）
            run_ticks(*s, 6);
            if (first_erase_bytes == 0 && flash.erase_calls != base_erases) {
                first_erase_bytes = flash.programmed_bytes;
            }
        }
    }
    const unsigned long total = flash.programmed_bytes;
    assert(first_erase_bytes != 0);
    // 寫入全部落地（不斷電時 6 個 tick 足夠）
    {
        flash.power_restore();
        StoragePtr check = boot();
        std::vector<uint8_t> expect(HAL_STORAGE_SIZE, 0);
        for (auto &st : steps) { memcpy(&expect[st.ofs], st.data, st.len); }
        assert(read_all(*check) == expect);
    }

    std::vector<unsigned long> cuts;
    for (unsigned i = 1; i <= 60; i++) { cuts.push_back(total * i / 61); }
    // 換扇區前後逐位元組密集取樣（含標頭、資料、有效旗標、換扇區與全量寫出）
    for (unsigned long b = first_erase_bytes - 200; b < first_erase_bytes + 200; b += 3) { cuts.push_back(b); }
    for (unsigned long b = 1; b < 120; b++) { cuts.push_back(b); }

    unsigned lost_power = 0;
    unsigned erase_window_resets = 0;
    for (unsigned long cut : cuts) {
        flash.reset();
        now_ms = 1;
        std::vector<uint8_t> committed(HAL_STORAGE_SIZE, 0);
        std::vector<uint8_t> in_flight;
        int in_flight_step = -1;
        {
            StoragePtr s = boot();
            const long budget = long(cut) - long(flash.programmed_bytes);
            flash.program_budget = budget < 0 ? 0 : budget;
            for (unsigned n = 0; n < writes && flash.powered; n++) {
                in_flight = committed;
                memcpy(&in_flight[steps[n].ofs], steps[n].data, steps[n].len);
                in_flight_step = int(n);
                s->write_block(steps[n].ofs, steps[n].data, steps[n].len);
                run_ticks(*s, 6);
                if (flash.powered) {
                    committed = in_flight;
                    in_flight_step = -1;
                }
            }
        }
        if (flash.powered) {
            continue;
        }
        lost_power++;
        // 斷電落在換扇區的抹除（含抹除後的扇區標頭）期間：AP_FlashStorage 開機時看到無效標頭會
        // 全部重設（erase_all）。這是共用程式的既有行為；可接受的結果只有「全部歸零」這種看得見的失敗。
        const bool in_erase_window = flash.died_on_erase || flash.programmed_since_erase < 4;
        flash.power_restore();
        StoragePtr rebooted = boot();
        const std::vector<uint8_t> got = read_all(*rebooted);
        assert(in_flight_step >= 0);
        if (in_erase_window && got == std::vector<uint8_t>(HAL_STORAGE_SIZE, 0)) {
            erase_window_resets++;
        } else for (unsigned line = 0; line < HAL_STORAGE_SIZE / 8; line++) {
            const bool old_ok = memcmp(&got[line * 8], &committed[line * 8], 8) == 0;
            const bool new_ok = memcmp(&got[line * 8], &in_flight[line * 8], 8) == 0;
            if (!old_ok && !new_ok) {
                fprintf(stderr, "cut=%lu line=%u step=%d\n", cut, line, in_flight_step);
            }
            assert(old_ok || new_ok);
        }
        // 重開機後照常可寫、可持久
        const uint8_t marker[8] = {9, 8, 7, 6, 5, 4, 3, 2};
        rebooted->write_block(64, marker, sizeof(marker));
        flush(*rebooted);
        assert(rebooted->healthy());
        rebooted.reset();
        StoragePtr again = boot();
        uint8_t back[8]{};
        again->read_block(back, 64, sizeof(back));
        assert(memcmp(back, marker, sizeof(marker)) == 0);
    }
    assert(lost_power == cuts.size());
    // 取樣確實落在換扇區的抹除期間
    assert(erase_window_resets > 0);
    // 斷電後 Flash 寫不進去，AP_FlashStorage 換扇區失敗會記錄內部錯誤（實機上會阻擋解鎖）
    AP_InternalError::recursion_errors = 0;
    check_region_only();
    printf("power-loss cuts: %u (total programmed bytes %lu, first sector erase at %lu), "
           "full reset from cuts in erase window: %u\n",
           lost_power, total, first_erase_bytes, erase_window_resets);
}

static void write_fault_recovery()
{
    flash.reset();
    now_ms = 1;
    StoragePtr s = boot();
    flush(*s);
    const uint8_t first[8] = {1, 1, 2, 3, 5, 8, 13, 21};
    s->write_block(40, first, sizeof(first));
    flush(*s);
    assert(s->healthy());

    // 整個區間暫時寫不進去：資料留在待寫，不宣稱健康
    flash.stuck_begin = RTL8735B_STORAGE_FLASH_BASE;
    flash.stuck_end = RTL8735B_STORAGE_FLASH_BASE + RTL8735B_STORAGE_FLASH_SIZE;
    const uint8_t second[8] = {2, 7, 1, 8, 2, 8, 1, 8};
    s->write_block(40, second, sizeof(second));
    run_ticks(*s, 3000);
    assert(!s->healthy());
    uint8_t ram[8]{};
    s->read_block(ram, 40, sizeof(ram));
    assert(memcmp(ram, second, sizeof(second)) == 0);

    // 故障排除後（上鎖狀態可重新初始化）自動補寫並恢復健康
    flash.stuck_begin = flash.stuck_end = 0;
    run_ticks(*s, 7000);
    flush(*s);
    assert(s->healthy());
    s.reset();
    StoragePtr rebooted = boot();
    uint8_t back[8]{};
    rebooted->read_block(back, 40, sizeof(back));
    assert(memcmp(back, second, sizeof(second)) == 0);
    AP_InternalError::recursion_errors = 0;
    check_region_only();
}

static void init_failure_is_ram_only()
{
    for (int mode = 0; mode < 2; mode++) {
        flash.reset();
        now_ms = 1;
        if (mode == 0) {
            flash.id[2] = 0x17;  // 8 MiB：選定區間不存在
        } else {
            flash.id_valid = false;
        }
        StoragePtr s = boot();
        const uint8_t v[3] = {4, 5, 6};
        s->write_block(10, v, sizeof(v));
        uint8_t back[3]{};
        s->read_block(back, 10, sizeof(back));
        assert(memcmp(back, v, sizeof(v)) == 0);
        flush(*s);
        assert(!s->healthy());
        assert(!s->erase());
        void *ptr = nullptr;
        size_t size = 0;
        assert(!s->get_storage_ptr(ptr, size));
        assert(flash.program_calls == 0 && flash.erase_calls == 0);
    }
}

static void erase_persists_zeros()
{
    flash.reset();
    now_ms = 1;
    StoragePtr s = boot();
    const uint8_t v[5] = {1, 2, 3, 4, 5};
    s->write_block(500, v, sizeof(v));
    flush(*s);
    assert(s->erase());
    flush(*s);
    assert(s->healthy());
    s.reset();
    StoragePtr rebooted = boot();
    assert(read_all(*rebooted) == std::vector<uint8_t>(HAL_STORAGE_SIZE, 0));
    check_region_only();
}

static void bounds()
{
    flash.reset();
    now_ms = 1;
    StoragePtr s = boot();
    const uint8_t sent[3] = {1, 2, 3};
    uint8_t received[3]{};
    bool rejected = false;
    try { s->write_block(HAL_STORAGE_SIZE - 1, sent, 3); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { s->read_block(received, 1, std::numeric_limits<size_t>::max()); }
    catch (const std::runtime_error &) { rejected = true; }
    assert(rejected);
    s->write_block(HAL_STORAGE_SIZE, sent, 0);
    s->read_block(nullptr, 0, 0);
    s->write_block(HAL_STORAGE_SIZE - 3, sent, 3);
    s->read_block(received, HAL_STORAGE_SIZE - 3, 3);
    assert(memcmp(sent, received, 3) == 0);
}

int main()
{
    region_layout();
    shim_contract();
    first_boot_and_persist();
    init_failure_is_ram_only();
    erase_persists_zeros();
    bounds();
    write_fault_recovery();
    sector_rotation_and_arming();
    power_loss_sweep();
    puts("PASS: flash storage shim guard/verify, persist across reboot, health, armed no-erase, "
         "sector rotation, power-loss atomicity, fault recovery, RAM-only fallback");
}
