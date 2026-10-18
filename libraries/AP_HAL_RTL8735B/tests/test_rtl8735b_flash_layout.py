#!/usr/bin/env python3
'''Flash 版面（hwdef/scripts/rtl8735b_flash_layout.py）、hwdef 的 FLASH_LAYOUT（rtl8735b_hwdef.py）與
映像關卡（Tools/ardupilotwaf/rtl8735b_image_check.py）的主機測試。

本儲存庫沒有 OTA：映像上限是 fw1 的長度，hwdef.h 不寫版面的常數。

有 AMEBAPRO2_SDK 時另外以 SDK 的分割表 JSON 為底產生分割表。
'''

import gc
import io
import json
import os
from contextlib import redirect_stdout
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest
import warnings

ROOT = Path(__file__).resolve().parents[3]   # 儲存庫根目錄（tests → AP_HAL_RTL8735B → libraries → 根）
HAL = ROOT / 'libraries/AP_HAL_RTL8735B'
sys.path.insert(0, str(HAL / 'hwdef/scripts'))
sys.path.insert(0, str(ROOT / 'Tools/ardupilotwaf'))
import rtl8735b_flash_layout as fl  # noqa: E402
import rtl8735b_hwdef  # noqa: E402
import rtl8735b_image_check as ic  # noqa: E402

SDK = os.environ.get('AMEBAPRO2_SDK')
SDK_TABLE = Path(SDK) / 'project/realtek_amebapro2_v0_example/GCC-RELEASE/mp/amebapro2_partitiontable.json' if SDK else None


class LayoutTest(unittest.TestCase):
    def test_all_layouts_valid(self):
        for name in fl.LAYOUTS:
            fl.validate(name)

    def test_unknown_layout(self):
        with self.assertRaisesRegex(fl.LayoutError, '不認得'):
            fl.validate('hub8735ultra')

    def test_default_is_realtek_ota(self):
        self.assertEqual(fl.DEFAULT, 'realtek-ota')

    def test_app_defines(self):
        # 開發板的應用程式讀得到的版面名稱與 fw1、fw2 的位址、長度（app/rtl8735b_app.h）
        self.assertEqual(fl.app_defines('realtek-ota'), {
            'RTL8735B_FLASH_LAYOUT': 'realtek-ota',
            'RTL8735B_FLASH_FW1_ADDR': '0x60000', 'RTL8735B_FLASH_FW1_LEN': '0x400000',
            'RTL8735B_FLASH_FW2_ADDR': '0x520000', 'RTL8735B_FLASH_FW2_LEN': '0x400000',
        })
        self.assertEqual(fl.app_defines('arduino-default')['RTL8735B_FLASH_FW2_LEN'], '0x10000')
        with self.assertRaisesRegex(fl.LayoutError, '不認得'):
            fl.app_defines('hub8735ultra')

    def test_slots(self):
        self.assertEqual((fl.LAYOUTS['realtek-ota']['fw1'], fl.LAYOUTS['realtek-ota']['fw2']),
                         ((0x60000, 0x400000), (0x520000, 0x400000)))
        self.assertEqual((fl.LAYOUTS['sdk-d1b6426']['fw1'], fl.LAYOUTS['sdk-d1b6426']['fw2']),
                         ((0x80000, 0x380000), (0x4C0000, 0x380000)))
        self.assertEqual(fl.LAYOUTS['arduino-default']['fw2'], (0x520000, 0x10000))

    def test_overlap_alignment_and_reserved_areas_rejected(self):
        saved = dict(fl.LAYOUTS)
        try:
            fl.LAYOUTS['bad-overlap'] = dict(fl.LAYOUTS['realtek-ota'], iq=(0x450000, 0xC0000))
            with self.assertRaisesRegex(fl.LayoutError, '重疊'):
                fl.validate('bad-overlap')
            fl.LAYOUTS['bad-align'] = dict(fl.LAYOUTS['realtek-ota'], fw2=(0x520800, 0x400000))
            with self.assertRaisesRegex(fl.LayoutError, '對齊'):
                fl.validate('bad-align')
            fl.LAYOUTS['bad-size'] = dict(fl.LAYOUTS['realtek-ota'], nn=(0x920000, 0x6E1000))
            with self.assertRaisesRegex(fl.LayoutError, '超出 Flash'):
                fl.validate('bad-size')
            fl.LAYOUTS['bad-storage'] = dict(fl.LAYOUTS['realtek-ota'], iq=(0xF70000, 0x20000))
            with self.assertRaisesRegex(fl.LayoutError, '參數區'):
                fl.validate('bad-storage')
            # SDK 的使用者資料（0xF00000–0xF64000）：nn 以外的記錄都不能蓋到；iq 緊接在 realtek-ota 的 nn 之後
            fl.LAYOUTS['bad-user-data'] = dict(fl.LAYOUTS['realtek-ota'], iq=(0xF00000, 0x10000))
            with self.assertRaisesRegex(fl.LayoutError, 'SDK 的使用者資料'):
                fl.validate('bad-user-data')
            fl.LAYOUTS['bad-user-data-tail'] = dict(fl.LAYOUTS['realtek-ota'], iq=(0xF63000, 0x1000))
            with self.assertRaisesRegex(fl.LayoutError, 'SDK 的使用者資料'):
                fl.validate('bad-user-data-tail')
            fl.LAYOUTS['after-user-data'] = dict(fl.LAYOUTS['realtek-ota'], iq=(0xF64000, 0x1000))
            fl.validate('after-user-data')
        finally:
            fl.LAYOUTS.clear()
            fl.LAYOUTS.update(saved)

    def test_nn_may_cover_reserved_areas(self):
        # ArduPilot 不燒 NN 模型區：arduino-default 的 nn 涵蓋參數區與 SDK 使用者資料，版面仍然有效
        nn = fl.LAYOUTS['arduino-default']['nn']
        self.assertTrue(nn[0] <= 0xF00000 and nn[0] + nn[1] >= 0xFA0000)
        fl.validate('arduino-default')

    def test_storage_matches_c_source(self):
        text = (HAL / 'storage_shim.h').read_text(encoding='utf-8')
        base = int(re.search(r'#define\s+RTL8735B_STORAGE_FLASH_BASE\s+(0x[0-9A-Fa-f]+)', text).group(1), 16)
        sector = int(re.search(r'#define\s+RTL8735B_STORAGE_FLASH_SECTOR_SIZE\s+(0x[0-9A-Fa-f]+)', text).group(1), 16)
        count = int(re.search(r'#define\s+RTL8735B_STORAGE_FLASH_SECTOR_COUNT\s+(\d+)', text).group(1))
        self.assertEqual(fl.STORAGE, (base, sector * count))


@unittest.skipUnless(SDK_TABLE and SDK_TABLE.is_file(), '需要 AMEBAPRO2_SDK')
class PartitionTableTest(unittest.TestCase):
    def sdk(self):
        return json.loads(SDK_TABLE.read_text(encoding='utf-8'))

    def test_sdk_layout_is_unchanged(self):
        self.assertEqual(fl.partition_table(self.sdk(), 'sdk-d1b6426'), self.sdk())

    def test_records_follow_layout(self):
        for name, records in fl.LAYOUTS.items():
            part = fl.partition_table(self.sdk(), name)['PARTAB']
            for rec, (addr, length) in records.items():
                self.assertEqual((int(part[rec]['start_addr'], 16), int(part[rec]['length'], 16)), (addr, length))
            self.assertEqual(part['table']['records'], self.sdk()['PARTAB']['table']['records'])

    def test_write_partition_table(self):
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / 'table.json'
            fl.write_partition_table(str(SDK_TABLE), 'realtek-ota', str(out))
            self.assertEqual(json.loads(out.read_text(encoding='utf-8')), fl.partition_table(self.sdk(), 'realtek-ota'))

    def test_missing_record_rejected(self):
        table = self.sdk()
        table['PARTAB']['table']['records'].remove('nn')
        with self.assertRaisesRegex(fl.LayoutError, 'nn'):
            fl.partition_table(table, 'realtek-ota')


class HwdefTest(unittest.TestCase):
    def run_hwdef(self, overlay_lines):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        overlay = Path(tmp.name) / 'overlay.dat'
        overlay.write_text('\n'.join(overlay_lines) + '\n', encoding='utf-8')
        h = rtl8735b_hwdef.RTL8735BHWDef(outdir=tmp.name, hwdef=[str(HAL / 'hwdef/rtl8735b-empty/hwdef.dat'),
                                                                 str(overlay)], quiet=True)
        buf = io.StringIO()
        # 上游 hwdef.py 開檔後不關（process_file、write_hwdef_header），回收時發 ResourceWarning；不是本庫的程式
        with redirect_stdout(buf), warnings.catch_warnings():
            warnings.simplefilter('ignore', ResourceWarning)
            h.run()
            gc.collect()
        return h, (Path(tmp.name) / 'hwdef.h').read_text(encoding='utf-8')

    def test_default_layout(self):
        h, header = self.run_hwdef([])
        self.assertEqual(h.env_vars['RTL8735B_FLASH_LAYOUT'], 'realtek-ota')
        # 韌體不用版面，hwdef.h 不寫版面的常數
        self.assertNotIn('RTL8735B_FLASH', header)

    def test_selected_layout(self):
        h, _ = self.run_hwdef(['FLASH_LAYOUT sdk-d1b6426'])
        self.assertEqual(h.env_vars['RTL8735B_FLASH_LAYOUT'], 'sdk-d1b6426')

    def test_later_overlay_wins(self):
        h, _ = self.run_hwdef(['FLASH_LAYOUT sdk-d1b6426', 'FLASH_LAYOUT arduino-default'])
        self.assertEqual(h.env_vars['RTL8735B_FLASH_LAYOUT'], 'arduino-default')

    def test_unknown_layout_fails(self):
        with self.assertRaises(SystemExit):
            self.run_hwdef(['FLASH_LAYOUT hub8735ultra'])

    def test_bad_keyword_usage_fails(self):
        with self.assertRaises(SystemExit):
            self.run_hwdef(['FLASH_LAYOUT'])


def flash_image(fw1, fw2, valid=(1, 1), size=0x3000):
    data = bytearray(b'\xff' * size)
    for (addr, length), off, v in ((fw1, 0x2060, valid[0]), (fw2, 0x2080, valid[1])):
        data[off:off + 16] = struct.pack('<II', addr, length) + b'\xc7\xc1' + b'\xff' * 5 + bytes([v])
    return bytes(data)


def slots(name):
    return fl.LAYOUTS[name]['fw1'], fl.LAYOUTS[name]['fw2']


class ImageCheckTest(unittest.TestCase):
    '''映像關卡比對的是版面表，不是建置目錄的分割表 JSON（那份是 SDK 從 POSTBUILD_PART 複製的）'''
    def files(self, d, fw_size, flash):
        paths = [Path(d) / n for n in ('firmware_ntz.bin', 'flash_ntz.bin')]
        paths[0].write_bytes(b'\0' * fw_size)
        paths[1].write_bytes(flash)
        return [str(p) for p in paths]

    def test_limit_is_fw1(self):
        # 本儲存庫沒有 OTA：映像上限是 fw1 的長度
        self.assertEqual(ic.image_limit(slots('realtek-ota')), 0x400000)
        self.assertEqual(ic.image_limit(slots('sdk-d1b6426')), 0x380000)
        self.assertEqual(ic.image_limit(slots('arduino-default')), 0x400000)

    def test_at_and_over_limit(self):
        r = fl.LAYOUTS['sdk-d1b6426']
        with tempfile.TemporaryDirectory() as d:
            fw, flash = self.files(d, 0x380000, flash_image(r['fw1'], r['fw2']))
            self.assertEqual(ic.check(fw, slots('sdk-d1b6426'), flash_path=flash), (0x380000, 0x380000))
            Path(fw).write_bytes(b'\0' * (0x380000 + 1))
            with self.assertRaisesRegex(ic.ImageCheckError, 'fw1 的長度'):
                ic.check(fw, slots('sdk-d1b6426'), flash_path=flash)

    def test_records_must_match(self):
        r = fl.LAYOUTS['realtek-ota']
        with tempfile.TemporaryDirectory() as d:
            fw, flash = self.files(d, 1000, flash_image(r['fw1'], r['fw2']))
            self.assertEqual(ic.check(fw, slots('realtek-ota'), flash_path=flash), (1000, 0x400000))
            for fw1 in (fl.LAYOUTS['sdk-d1b6426']['fw1'], (r['fw1'][0], r['fw1'][1] - 0x1000),
                        (r['fw1'][0] + 0x1000, r['fw1'][1])):
                Path(flash).write_bytes(flash_image(fw1, r['fw2']))
                with self.assertRaisesRegex(ic.ImageCheckError, 'fw1'):
                    ic.check(fw, slots('realtek-ota'), flash_path=flash)
            Path(flash).write_bytes(flash_image(r['fw1'], r['fw2'], valid=(1, 0)))
            with self.assertRaisesRegex(ic.ImageCheckError, 'fw2'):
                ic.check(fw, slots('realtek-ota'), flash_path=flash)

    def test_sdk_table_left_in_place_is_caught(self):
        # POSTBUILD_PART 的覆寫失效時，打包用的是 SDK 原本的分割表（sdk-d1b6426）：選了 realtek-ota 就要失敗
        s = fl.LAYOUTS['sdk-d1b6426']
        with tempfile.TemporaryDirectory() as d:
            fw, flash = self.files(d, 1000, flash_image(s['fw1'], s['fw2']))
            with self.assertRaisesRegex(ic.ImageCheckError, 'fw1'):
                ic.check(fw, slots('realtek-ota'), flash_path=flash)
            self.assertEqual(ic.check(fw, slots('sdk-d1b6426'), flash_path=flash), (1000, 0x380000))

    def test_arduino_default_builds(self):
        # 本儲存庫沒有 OTA，arduino-default 只看 fw1，64 KiB 的 fw2 不影響
        r = fl.LAYOUTS['arduino-default']
        with tempfile.TemporaryDirectory() as d:
            fw, flash = self.files(d, 3260416, flash_image(r['fw1'], r['fw2']))
            self.assertEqual(ic.check(fw, slots('arduino-default'), flash_path=flash), (3260416, 0x400000))


if __name__ == '__main__':
    unittest.main()
