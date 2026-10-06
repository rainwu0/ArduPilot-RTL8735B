/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/*
 * 參數儲存用外部 NOR Flash 的 C 包裝。SDK 的 flash_api.h 經 device.h 引入的標頭在 C++ 下有型別
 * 衝突（見 sdk_shim.h），所以 SDK 呼叫放在以 C 編譯的 storage_shim.c，C++ 端只看這個標頭。
 *
 * 區間選擇：
 *   - 分割表 GCC-RELEASE/mp/amebapro2_partitiontable.json 在 nn（到 0xE00000）之後只有
 *     mp（0xFC0000，valid=false）；
 *   - 範例專案 inc/platform_opts.h:16–26 的使用者資料在 0xF00000–0xF64000（Wi-Fi 快速連線、ISP、
 *     FCS、IQ），NOR 檔案系統起點 FLASH_APP_BASE 為 0xE00000；本映像沒有連結檔案系統；
 *   - 連結進 Copter 映像、會寫 Flash 的程式只寫 0xF00000–0xF50000 與 SPIC 校正值（0x3000 附近），
 *     bootloader 只寫 SPIC 校正值；
 *   - 燒錄的 flash_ntz.bin 只到 0x42E000。
 * 0xF80000–0xFA0000 落在上述之外。注意：Ameba-AIoT/ameba-rtos-pro2 的 NOR 檔案系統位於
 * 0xF64000–0x1000000，與本區間重疊；改用該 SDK 並啟用檔案系統時要重新選址。位址是 Flash 內位移，不是 CPU 位址。
 */

#include <stdint.h>

#define RTL8735B_STORAGE_FLASH_BASE         0x00F80000u
// AP_FlashStorage 的一個邏輯扇區由 16 個 4 KiB 的 NOR 抹除扇區組成
#define RTL8735B_STORAGE_FLASH_SECTOR_SIZE  0x00010000u
#define RTL8735B_STORAGE_FLASH_SECTOR_COUNT 2u
#define RTL8735B_STORAGE_FLASH_SIZE \
    (RTL8735B_STORAGE_FLASH_SECTOR_SIZE * RTL8735B_STORAGE_FLASH_SECTOR_COUNT)
#define RTL8735B_STORAGE_FLASH_ERASE_SIZE   0x00001000u

enum {
    RTL8735B_STORAGE_FLASH_OK = 0,
    // 參數錯誤或超出儲存區間：沒有碰 Flash
    RTL8735B_STORAGE_FLASH_EINVAL = -1,
    // 讀不到 Flash ID，或容量放不下儲存區間
    RTL8735B_STORAGE_FLASH_EID = -2,
    // 寫入或抹除後讀回不符（SDK 的寫入與抹除函式本身不回報錯誤）
    RTL8735B_STORAGE_FLASH_EVERIFY = -3,
};

#ifdef __cplusplus
extern "C" {
#endif

// 讀 JEDEC ID（id[0..2]），確認容量足以容納儲存區間
int rtl8735b_storage_flash_init(uint8_t id[3]);

// offset 為儲存區間內的位移；只在 task 中呼叫（會取 SDK 的 Flash device mutex）
int rtl8735b_storage_flash_read(uint32_t offset, uint8_t *data, uint32_t len);
// 寫入後讀回比對；NOR 只能把位元由 1 清為 0
int rtl8735b_storage_flash_program(uint32_t offset, const uint8_t *data, uint32_t len);
// offset、len 需為 4 KiB 的倍數；抹除後確認全為 0xFF
int rtl8735b_storage_flash_erase(uint32_t offset, uint32_t len);

#ifdef __cplusplus
}
#endif
