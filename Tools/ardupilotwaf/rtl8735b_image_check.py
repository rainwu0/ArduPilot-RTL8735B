# encoding: utf-8
# SPDX-License-Identifier: GPL-3.0-or-later

"""
RTL8735B 的映像關卡，比對 hwdef 的 FLASH_LAYOUT 選的版面（hwdef/scripts/rtl8735b_flash_layout.py 的 fw1、fw2）：
  - 大小：firmware_ntz.bin 不得超過 fw1 的長度。燒錄時韌體放在 fw1，槽內沒有其他資料。
  - 分割記錄：flash_ntz.bin 在 0x2060（fw1）、0x2080（fw2）的記錄要等於版面的位址與長度，而且是有效的
    （每筆 16 bytes：位址、長度，最後一個 byte 是 valid）。
    比的是版面表，不是建置目錄的 amebapro2_partitiontable.json：那份是 SDK 從 POSTBUILD_PART 複製來的，
    覆寫失效時它和 flash_ntz.bin 都會是 SDK 原本的分割表，比對兩者抓不到。

AP_FLAKE8_CLEAN
"""

import os
import struct

RECORDS = (('fw1', 0x2060), ('fw2', 0x2080))
RECORD_LEN = 16
VALID_OFFSET = 15
VALID = 0x01


class ImageCheckError(Exception):
    pass


def image_limit(slots):
    '''slots 是 ((fw1 位址, 長度), (fw2 位址, 長度))；上限是 fw1 的長度'''
    return slots[0][1]


def check_records(flash_path, slots):
    '''flash_ntz.bin 的 fw1、fw2 記錄要等於版面的位址與長度，且 valid'''
    with open(flash_path, 'rb') as f:
        head = f.read(RECORDS[-1][1] + RECORD_LEN)
    for (name, off), want in zip(RECORDS, slots):
        rec = head[off:off + RECORD_LEN]
        if len(rec) < RECORD_LEN:
            raise ImageCheckError('flash_ntz.bin 太短，沒有 %s 的分割記錄' % name)
        got = struct.unpack_from('<II', rec)
        if got != tuple(want) or rec[VALID_OFFSET] != VALID:
            raise ImageCheckError('flash_ntz.bin 的 %s 記錄（0x%X＋0x%X，valid 0x%02X）與版面（0x%X＋0x%X）不同，'
                                  '打包沒有用版面產生的分割表'
                                  % (name, got[0], got[1], rec[VALID_OFFSET], want[0], want[1]))


def check(firmware_path, slots, flash_path=None):
    limit = image_limit(slots)
    size = os.path.getsize(firmware_path)
    if size > limit:
        raise ImageCheckError('firmware_ntz.bin 為 %d bytes，超過 fw1 的長度 %d bytes' % (size, limit))
    if flash_path is not None:
        check_records(flash_path, slots)
    return size, limit
