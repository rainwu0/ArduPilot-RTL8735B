#!/usr/bin/env python3
'''ota_monocypher.c（以 C 再編一次 ArduPilot 的 Monocypher）的符號：SDK 的 component/mbed/targets/hal/rtl8735b/crypto_api.c
也定義了 crypto_poly1305、crypto_poly1305_init，同名會在連結映像時重複定義。接收端用不到 Poly1305，
所以這組函式在 ota_monocypher.h 改名；驗簽章用的 crypto_check_* 與 crypto_blake2b_* 照原名。'''

from pathlib import Path
import subprocess
import tempfile
import unittest

import ota_cc

# SDK 的 crypto_api.c 定義的名字（連結時撞到的是前兩個；Poly1305 的其餘兩個一起改名）
SDK_NAMES = {'crypto_poly1305', 'crypto_poly1305_init', 'crypto_poly1305_update', 'crypto_poly1305_final'}
NEEDED = {'crypto_check_init', 'crypto_check_update', 'crypto_check_final', 'crypto_blake2b_init',
          'crypto_blake2b_update', 'crypto_blake2b_final'}


@unittest.skipUnless(ota_cc.CAN_RUN, ota_cc.SKIP_REASON)
class MonocypherSymbolsTest(unittest.TestCase):
    def test_no_symbol_clashes_with_the_sdk(self):
        with tempfile.TemporaryDirectory() as tmp:
            obj = Path(tmp) / 'ota_monocypher.o'
            subprocess.run([ota_cc.CC, '-std=c99', '-w', '-c', str(ota_cc.OTA / 'ota_monocypher.c'), '-o', str(obj)],
                           check=True)
            out = subprocess.run(['nm', '-g', '--defined-only', str(obj)], capture_output=True, text=True, check=True)
        defined = {line.split()[-1] for line in out.stdout.splitlines() if line.strip()}
        self.assertEqual(defined & SDK_NAMES, set())
        self.assertLessEqual(NEEDED, defined)


if __name__ == '__main__':
    unittest.main()
