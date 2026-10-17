/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FatFs 的磁碟介面（FatFs 的 diskio.h），接到 sdcard_shim。只有實體磁碟 0，就是板上的 microSD。
 *
 * - 卡的初始化、關閉與互斥都由檔案系統後端 libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp 負責：
 *   掛載前它先呼叫 rtl8735b_sdcard_init()，讀寫出錯時關掉再重掛。disk_initialize 只回報狀態、
 *   不自己初始化卡，重試的時機與間隔留在後端一處決定。
 * - 時間戳 get_fattime() 也在後端（要用 ArduPilot 的時鐘）。
 * - 寫入在 rtl8735b_sdcard_write() 回傳時已交給卡，CTRL_SYNC 不必再做事。
 */

#include "hwdef.h"

#if defined(HAL_RTL8735B_SDCARD_ENABLED) && HAL_RTL8735B_SDCARD_ENABLED

#include <ff.h>
#include <diskio.h>

#include "sdcard_shim.h"

#define SDCARD_PDRV 0

static DRESULT _result(int ret)
{
    switch (ret) {
    case RTL8735B_SDCARD_OK:
        return RES_OK;
    case RTL8735B_SDCARD_EINVAL:
        return RES_PARERR;
    case RTL8735B_SDCARD_ENOCARD:
        return RES_NOTRDY;
    default:
        return RES_ERROR;
    }
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != SDCARD_PDRV || !rtl8735b_sdcard_ready()) {
        return STA_NOINIT;
    }
    return 0;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SDCARD_PDRV) {
        return RES_PARERR;
    }
    return _result(rtl8735b_sdcard_read((uint32_t)sector, buff, count));
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != SDCARD_PDRV) {
        return RES_PARERR;
    }
    return _result(rtl8735b_sdcard_write((uint32_t)sector, buff, count));
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != SDCARD_PDRV) {
        return RES_PARERR;
    }
    if (!rtl8735b_sdcard_ready()) {
        return RES_NOTRDY;
    }
    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = rtl8735b_sdcard_block_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = RTL8735B_SDCARD_BLOCK_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        // 抹除區塊大小不明：FatFs 規定回傳 1
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

#endif  // HAL_RTL8735B_SDCARD_ENABLED
