#!/usr/bin/env python3
"""Wi-Fi STA 帳密產生器的主機測試：格式驗證、未設定時停用、錯誤訊息不洩漏密碼、內容不變不重寫。"""

import os
from pathlib import Path
import re
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(os.environ['RTL8735B_HAL']) / 'hwdef/scripts'))
import rtl8735b_wifi_config as wc  # noqa: E402


def c_array(source, name):
    match = re.search(r'const uint8_t %s\[(\d+)\] = \{([^}]*)\};' % name, source)
    assert match, name
    values = [int(v, 16) for v in match.group(2).split(',') if v.strip()]
    return int(match.group(1)), bytes(values)


def c_scalar(source, name):
    match = re.search(r'const uint8_t %s = (\d+);' % name, source)
    assert match, name
    return int(match.group(1))


class WifiConfigTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='rtl8735b-wifi-config-')
        self.dir = Path(self.tmp.name)
        self.out = self.dir / 'out/wifi_credentials.c'

    def tearDown(self):
        self.tmp.cleanup()

    def write_config(self, text, encoding='utf-8'):
        path = self.dir / 'wifi-sta.conf'
        path.write_bytes(text.encode(encoding))
        return path

    def test_missing_path_generates_disabled_source(self):
        status = wc.generate(None, self.out)
        self.assertEqual(status, 'disabled')
        source = self.out.read_text(encoding='utf-8')
        self.assertEqual(c_scalar(source, 'rtl8735b_wifi_ssid_len'), 0)
        self.assertEqual(c_scalar(source, 'rtl8735b_wifi_password_len'), 0)

    def test_nonexistent_file_is_disabled_not_error(self):
        status = wc.generate(self.dir / 'absent.conf', self.out)
        self.assertEqual(status, 'disabled')
        self.assertEqual(c_scalar(self.out.read_text(encoding='utf-8'), 'rtl8735b_wifi_ssid_len'), 0)

    def test_wpa2_credentials_are_emitted_as_bytes(self):
        path = self.write_config('# 註解\n\nssid=Lab "Net"\\x\r\npassword=pa ss"word\\1\r\n')
        self.assertEqual(wc.generate(path, self.out), 'configured')
        source = self.out.read_text(encoding='utf-8')
        size, ssid = c_array(source, 'rtl8735b_wifi_ssid')
        self.assertEqual(size, 33)
        self.assertEqual(c_scalar(source, 'rtl8735b_wifi_ssid_len'), len('Lab "Net"\\x'))
        self.assertEqual(ssid[:c_scalar(source, 'rtl8735b_wifi_ssid_len')], b'Lab "Net"\\x')
        size, password = c_array(source, 'rtl8735b_wifi_password')
        self.assertEqual(size, 65)
        self.assertEqual(password[:c_scalar(source, 'rtl8735b_wifi_password_len')], b'pa ss"word\\1')
        # 帳密只以位元組陣列出現，C 原始碼裡沒有可直接搜尋到的明文字串。
        self.assertNotIn('pa ss', source)
        self.assertNotIn('Lab', source)

    def test_utf8_bom_and_utf8_ssid(self):
        path = self.write_config('\ufeffssid=實驗室\npassword=12345678\n')
        wc.generate(path, self.out)
        source = self.out.read_text(encoding='utf-8')
        length = c_scalar(source, 'rtl8735b_wifi_ssid_len')
        self.assertEqual(c_array(source, 'rtl8735b_wifi_ssid')[1][:length], '實驗室'.encode('utf-8'))

    def test_open_network_without_password(self):
        path = self.write_config('ssid=open-net\n')
        self.assertEqual(wc.generate(path, self.out), 'configured')
        self.assertEqual(c_scalar(self.out.read_text(encoding='utf-8'), 'rtl8735b_wifi_password_len'), 0)

    def test_64_hex_psk_is_accepted(self):
        path = self.write_config('ssid=n\npassword=' + 'a1' * 32 + '\n')
        self.assertEqual(wc.generate(path, self.out), 'configured')

    def assert_rejected(self, text, secret=None):
        path = self.write_config(text)
        with self.assertRaises(wc.WifiConfigError) as ctx:
            wc.generate(path, self.out)
        if secret is not None:
            self.assertNotIn(secret, str(ctx.exception))
        self.assertFalse(self.out.exists())

    def test_invalid_contents_fail_without_leaking_secret(self):
        self.assert_rejected('password=short7!\n', secret='short7!')             # 缺 SSID
        self.assert_rejected('ssid=\npassword=12345678\n')                       # 空 SSID
        self.assert_rejected('ssid=' + 'x' * 33 + '\n')                          # SSID 超過 32 bytes
        self.assert_rejected('ssid=n\npassword=1234567\n', secret='1234567')     # 密碼太短
        self.assert_rejected('ssid=n\npassword=' + 'y' * 65 + '\n', secret='y' * 65)
        self.assert_rejected('ssid=n\npassword=' + 'g' * 64 + '\n', secret='g' * 64)  # 64 字但非 hex
        self.assert_rejected('ssid=n\npassword=飛控飛控飛控\n', secret='飛控')    # 非 ASCII 密碼
        self.assert_rejected('ssid=n\nssid=m\n')                                 # 重複鍵
        self.assert_rejected('ssid=n\nchannel=6\n')                              # 未知鍵
        self.assert_rejected('ssid n\n')                                         # 沒有等號

    def test_unchanged_content_is_not_rewritten(self):
        path = self.write_config('ssid=n\npassword=12345678\n')
        wc.generate(path, self.out)
        os.utime(self.out, (1, 1))
        wc.generate(path, self.out)
        self.assertEqual(self.out.stat().st_mtime, 1)
        path.write_text('ssid=n\npassword=87654321\n', encoding='utf-8')
        wc.generate(path, self.out)
        self.assertNotEqual(self.out.stat().st_mtime, 1)

    def test_generated_source_compiles(self):
        path = self.write_config('ssid=n\npassword=12345678\n')
        wc.generate(path, self.out)
        import subprocess
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-c', str(self.out),
                        '-o', str(self.dir / 'wifi_credentials.o')], check=True)


if __name__ == '__main__':
    unittest.main()
