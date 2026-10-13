#pragma once
// 以 NOR 規則模擬 16 MiB 外部 Flash：抹除設為 0xFF（4 KiB），寫入只能把位元由 1 清為 0。
#include <stdint.h>
#include <vector>

struct StorageFlashFake {
    static constexpr uint32_t size = 16u * 1024u * 1024u;
    std::vector<uint8_t> mem = std::vector<uint8_t>(size, 0xFF);
    uint8_t id[3] = {0xEF, 0x40, 0x18};
    bool id_valid = true;
    // 斷電模擬：剩餘可寫入的位元組數；歸零後所有寫入與抹除都不生效（< 0 表示不限制）
    long program_budget = -1;
    bool powered = true;
    // 寫入故障：這個位址範圍內的位元寫不進去（例如壞區）
    uint32_t stuck_begin = 0, stuck_end = 0;
    unsigned erase_calls = 0;
    unsigned long programmed_bytes = 0;
    // 最近一次抹除之後成功寫入的位元組數，以及斷電是否發生在抹除操作上
    unsigned long programmed_since_erase = 0;
    bool died_on_erase = false;
    unsigned program_calls = 0;
    unsigned read_calls = 0;
    unsigned unlocked_ops = 0;
    unsigned unaligned_cache_ops = 0;
    uint32_t min_touched = UINT32_MAX;
    uint32_t max_touched = 0;
    int lock_depth = 0;

    void reset() { *this = StorageFlashFake(); }
    void power_restore() { powered = true; program_budget = -1; }
};

extern StorageFlashFake storage_flash_fake;
