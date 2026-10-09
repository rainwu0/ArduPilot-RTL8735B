#!/usr/bin/env python3
# encoding: utf-8
# SPDX-License-Identifier: GPL-3.0-or-later

"""
RTL8735B 的 Flash 版面：分割表各記錄的位址與長度，以及建置時給 SDK 打包用的分割表 JSON。

版面依來源命名。各套件的分割表不是每塊板子一份，只由套件的 OTA Mode 選項決定：
  realtek-ota（預設）  Realtek 的 Arduino 套件 OTA Mode=Enable 用的 amebapro2_partitiontable_OTA.json
                       （Ameba-AIoT/ameba-arduino-pro2 93d6351），與 Ameba-AIoT/ameba-rtos-pro2 eb5c090 的範例分割相同；
                       HUB 8735 ultra 板商套件 4.1.1（ideashatch/HUB-8735 870a7e0 的發行包）也是這一份。
  sdk-d1b6426          ambpro2_sdk d1b6426 範例專案的 amebapro2_partitiontable.json。
  arduino-default      同一個 Arduino 套件 OTA Mode=Disable 用的 amebapro2_partitiontable.json（fw2 只有 64 KiB）。

建置時以使用者 SDK 自己的分割表 JSON 為底，只改寫 PARTAB 中各記錄的位址與長度，其他內容照舊。

AP_FLAKE8_CLEAN
"""

import json

DEFAULT = 'realtek-ota'

SECTOR = 0x1000
FLASH_SIZE = 0x1000000
# HAL 的參數區，同 libraries/AP_HAL_RTL8735B/storage_shim.h
STORAGE = (0xF80000, 0x20000)
# SDK 的使用者資料（Wi-Fi 快速連線、ISP、校正 IQ），platform_opts.h
SDK_USER_DATA = (0xF00000, 0x64000)

_COMMON = {
    'sysdata': (0x7000, 0x1000),
    'fcsdata': (0x8000, 0x1000),
    'boot_p': (0x9000, 0x27000),
    'boot_s': (0x30000, 0x27000),
}

LAYOUTS = {
    'realtek-ota': dict(_COMMON, fw1=(0x60000, 0x400000), iq=(0x460000, 0xC0000), fw2=(0x520000, 0x400000),
                        nn=(0x920000, 0x5E0000)),
    'sdk-d1b6426': dict(_COMMON, fw1=(0x80000, 0x380000), iq=(0x400000, 0xC0000), fw2=(0x4C0000, 0x380000),
                        nn=(0x840000, 0x5C0000)),
    'arduino-default': dict(_COMMON, fw1=(0x60000, 0x400000), iq=(0x460000, 0xC0000), fw2=(0x520000, 0x10000),
                            nn=(0x530000, 0xA90000)),
}

# 可以蓋過參數區與 SDK 使用者資料的記錄：ArduPilot 不燒 NN 模型區（arduino-default 的 nn 涵蓋這兩段）
_MAY_COVER_RESERVED = ('nn',)


class LayoutError(Exception):
    pass


def _overlaps(a, b):
    return a[0] < b[0] + b[1] and b[0] < a[0] + a[1]


def get(name):
    try:
        return LAYOUTS[name]
    except KeyError:
        raise LayoutError('不認得的 FLASH_LAYOUT %r，可用：%s' % (name, '、'.join(sorted(LAYOUTS))))


def validate(name):
    '''檢查版面本身：對齊、在 Flash 內、記錄不重疊、不蓋到參數區與 SDK 使用者資料；不合時丟 LayoutError'''
    records = get(name)
    items = sorted(records.items(), key=lambda kv: kv[1][0])
    for rec, (addr, length) in items:
        if addr % SECTOR or length % SECTOR or length == 0 or addr + length > FLASH_SIZE:
            raise LayoutError('%s 的 %s（0x%X＋0x%X）沒有對齊 4 KiB 或超出 Flash' % (name, rec, addr, length))
    for (ra, a), (rb, b) in zip(items, items[1:]):
        if _overlaps(a, b):
            raise LayoutError('%s 的 %s 與 %s 重疊' % (name, ra, rb))
    for rec, area in records.items():
        if rec in _MAY_COVER_RESERVED:
            continue
        for label, reserved in (('參數區', STORAGE), (' SDK 的使用者資料', SDK_USER_DATA)):
            if _overlaps(area, reserved):
                raise LayoutError('%s 的 %s 與%s 0x%X–0x%X 重疊'
                                  % (name, rec, label, reserved[0], reserved[0] + reserved[1]))


def partition_table(sdk_table, name):
    '''以 SDK 的分割表（json.load 的結果）為底，改寫 PARTAB 各記錄的位址與長度，回傳新的 dict'''
    table = json.loads(json.dumps(sdk_table))
    try:
        partab = table['PARTAB']
        listed = partab['table']['records']
    except (KeyError, TypeError):
        raise LayoutError('SDK 的分割表沒有 PARTAB 或記錄清單')
    for rec, (addr, length) in get(name).items():
        if rec not in listed or rec not in partab:
            raise LayoutError('SDK 的分割表沒有 %s 記錄' % rec)
        partab[rec]['start_addr'] = '0x%X' % addr
        partab[rec]['length'] = '0x%X' % length
        partab[rec]['valid'] = True
    return table


def write_partition_table(sdk_json_path, name, out_path):
    with open(sdk_json_path, encoding='utf-8') as f:
        table = partition_table(json.load(f), name)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(table, f, indent=4)
        f.write('\n')
