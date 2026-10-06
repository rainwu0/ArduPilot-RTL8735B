/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <AP_HAL/AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_RTL8735B
#include "Storage.h"
#include <stdio.h>
#include <string.h>

extern const AP_HAL::HAL& hal;

using namespace RTL8735B;

void Storage::init()
{
    _storage_open();
}

/*
  第一次使用時載入 Flash 內容。AP_FlashStorage::init() 在標頭無效（第一次開機）或有扇區已滿時
  會抹除；此時在呼叫者（開機時的主執行緒）中同步進行。
 */
void Storage::_storage_open(void)
{
    if (_backend != Backend::None) {
        return;
    }
    WITH_SEMAPHORE(_sem);
    if (_backend != Backend::None) {
        return;
    }
    _dirty_mask.clearall();
    uint8_t id[3] {};
    const int ret = rtl8735b_storage_flash_init(id);
    if (ret == RTL8735B_STORAGE_FLASH_OK && _flash.init()) {
        ::printf("Storage: flash id %02x %02x %02x, using 0x%06lx-0x%06lx\n",
                 id[0], id[1], id[2],
                 (unsigned long)RTL8735B_STORAGE_FLASH_BASE,
                 (unsigned long)(RTL8735B_STORAGE_FLASH_BASE + RTL8735B_STORAGE_FLASH_SIZE));
        _backend = Backend::Flash;
        return;
    }
    // 讀不到或寫不進 Flash：參數只在本次開機有效，healthy() 維持 false 讓解鎖檢查看見
    ::printf("Storage: flash unavailable (id %02x %02x %02x, err %d), parameters not persisted\n",
             id[0], id[1], id[2], ret);
    memset(_buffer, 0, sizeof(_buffer));
    _backend = Backend::RamOnly;
}

void Storage::_mark_dirty(uint16_t loc, uint16_t length)
{
    if (length == 0) {
        return;
    }
    const uint16_t end = loc + length - 1;
    for (uint16_t line = loc >> LINE_SHIFT; line <= (end >> LINE_SHIFT); line++) {
        _dirty_mask.set(line);
    }
}

void Storage::read_block(void *dst, uint16_t src, size_t length)
{
    if (dst == nullptr || length == 0) {
        return;
    }
    if (src > sizeof(_buffer) || length > sizeof(_buffer) - src) {
        AP_HAL::panic("Storage: read outside parameter buffer");
        return;
    }
    _storage_open();
    WITH_SEMAPHORE(_sem);
    memcpy(dst, &_buffer[src], length);
}

void Storage::write_block(uint16_t offset, const void *src, size_t length)
{
    if (src == nullptr || length == 0) {
        return;
    }
    if (offset > sizeof(_buffer) || length > sizeof(_buffer) - offset) {
        AP_HAL::panic("Storage: write outside parameter buffer");
        return;
    }
    _storage_open();
    WITH_SEMAPHORE(_sem);
    if (memcmp(src, &_buffer[offset], length) == 0) {
        return;
    }
    memcpy(&_buffer[offset], src, length);
    if (_backend == Backend::Flash) {
        _mark_dirty(offset, uint16_t(length));
    }
}

/*
  由 APM_STORAGE task 呼叫：每次寫出一行，限制單次延遲。寫出前在鎖內複製該行，
  寫完後若緩衝區在期間又被改過，就保留髒標記下次重寫。
 */
void Storage::_timer_tick(void)
{
    if (_backend != Backend::Flash) {
        return;
    }
    int16_t line;
    {
        WITH_SEMAPHORE(_sem);
        if (_dirty_mask.empty()) {
            _last_empty_ms = AP_HAL::millis();
            return;
        }
        line = _dirty_mask.first_set();
        if (line < 0 || line >= NUM_LINES) {
            return;
        }
        memcpy(_tmpline, &_buffer[LINE_SIZE * line], LINE_SIZE);
    }

    if (!_flash.write(LINE_SIZE * line, LINE_SIZE)) {
        return;
    }

    WITH_SEMAPHORE(_sem);
    if (memcmp(_tmpline, &_buffer[LINE_SIZE * line], LINE_SIZE) == 0) {
        _dirty_mask.clear(line);
    }
}

bool Storage::_flash_write_data(uint8_t sector, uint32_t offset, const uint8_t *data, uint16_t length)
{
    if (sector >= RTL8735B_STORAGE_FLASH_SECTOR_COUNT ||
        offset > RTL8735B_STORAGE_FLASH_SECTOR_SIZE ||
        length > RTL8735B_STORAGE_FLASH_SECTOR_SIZE - offset) {
        return false;
    }
    const bool ok = rtl8735b_storage_flash_program(sector * RTL8735B_STORAGE_FLASH_SECTOR_SIZE + offset,
                                                    data, length) == RTL8735B_STORAGE_FLASH_OK;
    if (!ok && _flash_erase_ok()) {
        // 上鎖時寫入失敗：每 5 秒最多一次以目前緩衝區重寫整個 Flash 區
        const uint32_t now = AP_HAL::millis();
        if (now - _last_re_init_ms > 5000) {
            _last_re_init_ms = now;
            const bool reinit = _flash.re_initialise();
            ::printf("Storage: failed at %u:%u for %u - re-init %u\n",
                     (unsigned)sector, (unsigned)offset, (unsigned)length, (unsigned)reinit);
        }
    }
    return ok;
}

bool Storage::_flash_read_data(uint8_t sector, uint32_t offset, uint8_t *data, uint16_t length)
{
    if (sector >= RTL8735B_STORAGE_FLASH_SECTOR_COUNT ||
        offset > RTL8735B_STORAGE_FLASH_SECTOR_SIZE ||
        length > RTL8735B_STORAGE_FLASH_SECTOR_SIZE - offset) {
        return false;
    }
    return rtl8735b_storage_flash_read(sector * RTL8735B_STORAGE_FLASH_SECTOR_SIZE + offset,
                                        data, length) == RTL8735B_STORAGE_FLASH_OK;
}

bool Storage::_flash_erase_sector(uint8_t sector)
{
    if (sector >= RTL8735B_STORAGE_FLASH_SECTOR_COUNT) {
        return false;
    }
    return rtl8735b_storage_flash_erase(sector * RTL8735B_STORAGE_FLASH_SECTOR_SIZE,
                                         RTL8735B_STORAGE_FLASH_SECTOR_SIZE) == RTL8735B_STORAGE_FLASH_OK;
}

// 抹除 64 KiB 需要多次 NOR 扇區抹除，只在上鎖（disarmed）時允許
bool Storage::_flash_erase_ok(void)
{
    return !hal.util->get_soft_armed();
}

/*
  最近 2 秒內曾經沒有待寫資料才算健康；Flash 無法使用時一律不健康
 */
bool Storage::healthy(void)
{
    return _backend == Backend::Flash && AP_HAL::millis() - _last_empty_ms < 2000U;
}

/*
  清除全部參數：寫入全零並由 storage task 寫出（AP_HAL::Storage 的預設做法）。
  Flash 無法使用時不能保證清除結果，回報失敗。
 */
bool Storage::erase()
{
    _storage_open();
    if (_backend != Backend::Flash) {
        return false;
    }
    return AP_HAL::Storage::erase();
}

bool Storage::get_storage_ptr(void *&ptr, size_t &size)
{
    if (_backend != Backend::Flash) {
        return false;
    }
    ptr = _buffer;
    size = sizeof(_buffer);
    return true;
}
#endif
