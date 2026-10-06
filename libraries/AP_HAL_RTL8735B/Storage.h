/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <AP_HAL/Storage.h>
#include <AP_HAL/AP_HAL_Boards.h>
#include <AP_Common/Bitmask.h>
#include <AP_FlashStorage/AP_FlashStorage.h>
#include <AP_HAL_RTL8735B/Semaphores.h>
#include "storage_shim.h"

namespace RTL8735B {

/*
  參數儲存：RAM 緩衝區加 AP_FlashStorage，寫到外部 NOR 的固定區間（storage_shim.h）。
  write_block 只改 RAM 並標記髒行，
  storage task 每次 _timer_tick 寫出一行；抹除只在上鎖（disarmed）時進行。
  Flash 無法使用時退回只存在 RAM，healthy() 與 erase() 回報 false，不把 RAM 當作已保存。
 */
class Storage : public AP_HAL::Storage {
public:
    void init() override;
    bool erase() override;
    void read_block(void *dst, uint16_t src, size_t length) override;
    void write_block(uint16_t offset, const void *src, size_t length) override;
    void _timer_tick(void) override;
    bool healthy() override;
    bool get_storage_ptr(void *&ptr, size_t &size) override;

private:
    static const uint8_t LINE_SHIFT = 3;
    static const uint16_t LINE_SIZE = 1U << LINE_SHIFT;
    static const uint16_t NUM_LINES = HAL_STORAGE_SIZE / LINE_SIZE;
    static_assert(HAL_STORAGE_SIZE % LINE_SIZE == 0, "Storage is not multiple of line size");

    enum class Backend : uint8_t {
        None,
        Flash,
        RamOnly,
    };

    void _storage_open(void);
    void _mark_dirty(uint16_t loc, uint16_t length);
    bool _flash_write_data(uint8_t sector, uint32_t offset, const uint8_t *data, uint16_t length);
    bool _flash_read_data(uint8_t sector, uint32_t offset, uint8_t *data, uint16_t length);
    bool _flash_erase_sector(uint8_t sector);
    bool _flash_erase_ok(void);

    uint8_t _buffer[HAL_STORAGE_SIZE] __attribute__((aligned(4)));
    uint8_t _tmpline[LINE_SIZE];
    Bitmask<NUM_LINES> _dirty_mask;
    Semaphore _sem;
    volatile Backend _backend = Backend::None;
    uint32_t _last_empty_ms = 0;
    uint32_t _last_re_init_ms = 0;

    AP_FlashStorage _flash{_buffer,
                           RTL8735B_STORAGE_FLASH_SECTOR_SIZE,
                           FUNCTOR_BIND_MEMBER(&Storage::_flash_write_data, bool, uint8_t, uint32_t, const uint8_t *, uint16_t),
                           FUNCTOR_BIND_MEMBER(&Storage::_flash_read_data, bool, uint8_t, uint32_t, uint8_t *, uint16_t),
                           FUNCTOR_BIND_MEMBER(&Storage::_flash_erase_sector, bool, uint8_t),
                           FUNCTOR_BIND_MEMBER(&Storage::_flash_erase_ok, bool)};
};

}
