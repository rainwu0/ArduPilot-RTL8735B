#!/usr/bin/env python3
'''OTA 公鑰產生器（hwdef/scripts/rtl8735b_ota_key.py）的主機測試：公鑰檔格式、沒指定時長度 0、
拒絕私鑰檔且錯誤訊息不含金鑰內容、內容不變不重寫；以及建置把產生的檔只編進開發板的應用程式。'''

import base64
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]   # 儲存庫根目錄（tests → AP_HAL_RTL8735B → libraries → 根）
HAL = ROOT / 'libraries/AP_HAL_RTL8735B'
sys.path.insert(0, str(HAL / 'hwdef/scripts'))
import rtl8735b_ota_key as ok  # noqa: E402

KEY = bytes(range(1, 33))


def key_file(directory, text):
    path = Path(directory) / 'key.dat'
    path.write_text(text, encoding='utf-8')
    return path


def c_key(source):
    '''產生的 C 檔 → (陣列長度, 陣列內容, rtl8735b_ota_public_key_len)'''
    m = re.search(r'const uint8_t rtl8735b_ota_public_key\[(\d+)\] = \{([^}]*)\};', source)
    assert m, source
    values = bytes(int(v, 16) for v in m.group(2).split(',') if v.strip())
    n = re.search(r'const uint32_t rtl8735b_ota_public_key_len = (\d+);', source)
    assert n, source
    return int(m.group(1)), values, int(n.group(1))


class OtaKeyTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='rtl8735b-ota-key-')
        self.dir = Path(self.tmp.name)
        self.out = self.dir / 'out/ota_public_key.c'

    def tearDown(self):
        self.tmp.cleanup()

    def test_public_key(self):
        path = key_file(self.dir, 'PUBLIC_KEYV1:' + base64.b64encode(KEY).decode() + '\n')
        self.assertEqual(ok.generate(path, self.out), 'configured')
        self.assertEqual(c_key(self.out.read_text(encoding='utf-8')), (32, KEY, 32))

    def test_no_key_gives_length_zero(self):
        # 沒指定時接收端一律拒收（NO_KEY）：長度 0，陣列全 0
        self.assertEqual(ok.generate(None, self.out), 'none')
        self.assertEqual(c_key(self.out.read_text(encoding='utf-8')), (32, bytes(32), 0))

    def test_missing_file_is_an_error(self):
        # 指定了卻找不到：不能悄悄建出不收任何映像的韌體
        with self.assertRaises(ok.OtaKeyError):
            ok.generate(self.dir / 'missing.dat', self.out)
        self.assertFalse(self.out.exists())

    def test_private_key_is_refused_without_echoing_it(self):
        secret = base64.b64encode(bytes(range(100, 132))).decode()
        path = key_file(self.dir, 'PRIVATE_KEYV1:' + secret)
        with self.assertRaises(ok.OtaKeyError) as cm:
            ok.generate(path, self.out)
        self.assertIn('私鑰', str(cm.exception))
        self.assertNotIn(secret, str(cm.exception))
        self.assertNotIn(secret[:8], str(cm.exception))
        self.assertFalse(self.out.exists())

    def test_bad_files(self):
        for text in ('', 'PUBLIC_KEYV1:', 'PUBLIC_KEYV1:not base64!',
                     'PUBLIC_KEYV1:' + base64.b64encode(bytes(31)).decode(),
                     'PUBLIC_KEYV1:' + base64.b64encode(bytes(33)).decode(),
                     base64.b64encode(KEY).decode()):
            with self.subTest(text=text):
                with self.assertRaises(ok.OtaKeyError):
                    ok.generate(key_file(self.dir, text), self.out)

    def test_unchanged_content_is_not_rewritten(self):
        path = key_file(self.dir, 'PUBLIC_KEYV1:' + base64.b64encode(KEY).decode())
        ok.generate(path, self.out)
        before = self.out.stat().st_mtime_ns
        ok.generate(path, self.out)
        self.assertEqual(self.out.stat().st_mtime_ns, before)


class BuildWiringTest(unittest.TestCase):
    '''waf 把產生的檔交給 cmake；cmake 只在有應用程式時編進去，沒有應用程式的韌體不變'''

    def test_waf_passes_the_generated_file(self):
        text = (ROOT / 'Tools/ardupilotwaf/rtl8735b.py').read_text(encoding='utf-8')
        self.assertIn("os.environ.get('RTL8735B_OTA_PUBLIC_KEY')", text)
        self.assertIn("lib_vars['RTL8735B_OTA_PUBLIC_KEY_SOURCE']", text)

    def test_cmake_adds_it_only_with_an_application(self):
        text = (HAL / 'targets/cmake/CMakeLists.txt').read_text(encoding='utf-8')
        block = re.search(r'^if\(RTL8735B_APP_DIR\)\n(.*?)^endif\(\)', text, re.S | re.M)
        self.assertIsNotNone(block)
        self.assertIn('${RTL8735B_OTA_PUBLIC_KEY_SOURCE}', block.group(1))
        outside = text[:block.start()] + text[block.end():]
        self.assertNotIn('${RTL8735B_OTA_PUBLIC_KEY_SOURCE}', outside)


if __name__ == '__main__':
    unittest.main()
