#!/usr/bin/env python3
'''libraries/AP_HAL_RTL8735B/app/ota/ota_flash.c的主機測試：以 flash_fakes/ 的假 SDK 標頭編譯 ota_flash.c 與
ota_flash_test.c（假 NOR Flash 加上 XIP 位址對映），逐案例執行。從 fw2 執行時讀 fw1 要讀到實體內容。'''

import tempfile
import unittest

import ota_cc

CASES = [
    'read_ignores_xip_remap', 'update_fw1_while_running_fw2', 'program_without_erase_fails',
    'lock_only_in_task_mode', 'flash_size_from_jedec',
]


@unittest.skipUnless(ota_cc.CAN_RUN, ota_cc.SKIP_REASON)
class OtaFlashTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = ota_cc.compile_exe(cls.tmp.name, 'ota_flash_test', ['ota_flash.c'], 'ota_flash_test.c',
                                     include_dirs=[ota_cc.TESTS / 'flash_fakes'])

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_case_list_matches(self):
        out = ota_cc.run(self.exe)
        self.assertEqual(out.stdout.split(), CASES)

    def run_case(self, name):
        out = ota_cc.run(self.exe, name)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertEqual(out.stdout.strip(), 'ok')


for _name in CASES:
    setattr(OtaFlashTest, 'test_' + _name, lambda self, n=_name: self.run_case(n))


if __name__ == '__main__':
    unittest.main()
