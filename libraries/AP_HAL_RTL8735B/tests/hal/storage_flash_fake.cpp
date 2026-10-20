/* SPDX-License-Identifier: GPL-3.0-or-later */
// SDK flash_api／device_lock／hal_cache 的主機替身，行為見 storage_fakes/sdk/storage_flash_fake.h。
#include "storage_fakes/sdk/flash_api.h"
#include "storage_fakes/sdk/objects.h"
#include "storage_fakes/sdk/device_lock.h"
#include "storage_fakes/sdk/hal_cache.h"
#include "storage_fakes/sdk/storage_flash_fake.h"

#include <assert.h>
#include <string.h>

StorageFlashFake storage_flash_fake;

namespace {
StorageFlashFake &f = storage_flash_fake;

void note_op()
{
    if (f.lock_depth <= 0) {
        f.unlocked_ops++;
    }
}

void touch(uint32_t address, uint32_t len)
{
    if (len == 0) {
        return;
    }
    if (address < f.min_touched) {
        f.min_touched = address;
    }
    if (address + len - 1 > f.max_touched) {
        f.max_touched = address + len - 1;
    }
}

bool stuck(uint32_t address)
{
    return address >= f.stuck_begin && address < f.stuck_end;
}

// 斷電後不再有任何寫入；回傳這次操作能否生效
bool consume_budget()
{
    if (!f.powered) {
        return false;
    }
    if (f.program_budget == 0) {
        f.powered = false;
        return false;
    }
    if (f.program_budget > 0) {
        f.program_budget--;
    }
    return true;
}
}

extern "C" {

void flash_erase_sector(flash_t *, uint32_t address)
{
    note_op();
    f.erase_calls++;
    if (f.on_erase != nullptr) {
        f.on_erase(f.on_erase_ctx);
    }
    const uint32_t base = address & ~0xFFFu;
    touch(base, 0x1000);
    assert(base + 0x1000 <= f.size);
    const bool was_powered = f.powered;
    if (!consume_budget()) {
        if (was_powered) {
            f.died_on_erase = true;
        }
        return;
    }
    f.programmed_since_erase = 0;
    for (uint32_t a = base; a < base + 0x1000; a++) {
        if (!stuck(a)) {
            f.mem[a] = 0xFF;
        }
    }
}

int flash_read_id(flash_t *, uint8_t *buf, uint8_t len)
{
    if (!f.id_valid) {
        return -1;
    }
    memcpy(buf, f.id, len < 3 ? len : 3);
    return len;
}

int flash_stream_read(flash_t *, uint32_t address, uint32_t len, uint8_t *data)
{
    note_op();
    f.read_calls++;
    assert(address + len <= f.size);
    memcpy(data, &f.mem[address], len);
    return 1;
}

int flash_stream_write(flash_t *, uint32_t address, uint32_t len, uint8_t *data)
{
    note_op();
    f.program_calls++;
    touch(address, len);
    assert(address + len <= f.size);
    for (uint32_t i = 0; i < len; i++) {
        if (!consume_budget()) {
            return 1;
        }
        f.programmed_bytes++;
        f.programmed_since_erase++;
        if (!stuck(address + i)) {
            f.mem[address + i] &= data[i];
        }
    }
    return 1;
}

void device_mutex_lock(uint32_t device)
{
    assert(device == RT_DEV_LOCK_FLASH);
    f.lock_depth++;
}

void device_mutex_unlock(uint32_t device)
{
    assert(device == RT_DEV_LOCK_FLASH);
    assert(f.lock_depth > 0);
    f.lock_depth--;
}

void dcache_clean_invalidate_by_addr(uint32_t *addr, int32_t dsize)
{
    if (((uintptr_t)addr & 31u) != 0 || (dsize & 31) != 0) {
        f.unaligned_cache_ops++;
    }
}

}
