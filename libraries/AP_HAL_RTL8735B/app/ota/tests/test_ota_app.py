#!/usr/bin/env python3
'''libraries/AP_HAL_RTL8735B/app/ota/ota_app.c 的主機測試：真的 ota_core.c 與真的 rtl8735b_app.h，加上 FreeRTOS、SDK、
rtl8735b_ardupilot_*、TCP 與連線的替身（app_fakes/），逐案例執行 ota_app_test.c。'''

from pathlib import Path
import tempfile
import unittest

import ota_cc

CASES = [
    'normal_boot', 'trial_boot_blocks_arming_and_starts_checks', 'trial_check_failure_blocks_arming',
    'rollback_resets_before_anything_else',
    'layout_mismatch_is_reported', 'confirm_after_60_s_when_ready', 'no_confirm_without_listening',
    'no_confirm_when_main_loop_stalls', 'no_confirm_without_wifi', 'trial_timeout_resets_only_when_disarmed',
    'reboot_order_after_install', 'reboot_after_peer_close_timeout', 'reboot_skipped_when_armed', 'refused_update_clears_progress',
    'receiver_starts_once', 'no_listen_without_wifi', 'probe_is_logged',
]

# 版面 realtek-ota，和 ota_app_test.c 的假 Flash 相同
LAYOUT_DEFINES = ['-DRTL8735B_FLASH_LAYOUT_NAME="realtek-ota"', '-DRTL8735B_FLASH_FW1_ADDR=0x60000',
                  '-DRTL8735B_FLASH_FW1_LEN=0x400000', '-DRTL8735B_FLASH_FW2_ADDR=0x520000',
                  '-DRTL8735B_FLASH_FW2_LEN=0x400000']


@unittest.skipUnless(ota_cc.CAN_RUN, ota_cc.SKIP_REASON)
class OtaAppTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = ota_cc.compile_exe(cls.tmp.name, 'ota_app_test', ['ota_app.c', 'ota_core.c'],
                                     'ota_app_test.c', include_dirs=[ota_cc.TESTS / 'app_fakes'],
                                     defines=[*LAYOUT_DEFINES, '-Dprintf=fake_printf'])

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
    setattr(OtaAppTest, 'test_' + _name, lambda self, n=_name: self.run_case(n))


@unittest.skipUnless(ota_cc.CAN_RUN, ota_cc.SKIP_REASON)
class BootSequenceTest(unittest.TestCase):
    '''跨開機的序列（輸出以文字模式讀，CRLF 讀成 LF）：每次開機一個行程（ota_app_test.c 的 seq_*），假 Flash 存成檔案。
    bootloader 從標籤有效的槽中選新裝的那一槽
    （fw2 有效就從 fw2，否則 fw1）。驗證維護者不在現場時第一次上板唯一的退路：新映像的接收端沒起來，
    就只靠試開逾時與退回回到舊槽；以及健康的新映像確認之後，下一次開機不再試開'''

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = ota_cc.compile_exe(cls.tmp.name, 'ota_app_seq', ['ota_app.c', 'ota_core.c'],
                                     'ota_app_test.c', include_dirs=[ota_cc.TESTS / 'app_fakes'],
                                     defines=[*LAYOUT_DEFINES, '-Dprintf=fake_printf'])

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.flash = str(Path(self.dir.name) / 'flash.bin')
        out = ota_cc.run(self.exe, 'seq_install', self.flash)
        self.assertEqual(out.stdout.strip(), 'installed', out.stdout + out.stderr)

    def labels(self):
        out = ota_cc.run(self.exe, 'seq_labels', self.flash)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        f = out.stdout.split()
        return f[1] == '1', f[3] == '1'

    def boot(self, mode):
        '''bootloader 選槽後開機一次，回傳（開機的槽, 輸出）'''
        fw1_valid, fw2_valid = self.labels()
        running = 2 if fw2_valid else 1
        self.assertTrue(fw1_valid or fw2_valid)
        out = ota_cc.run(self.exe, 'seq_boot', self.flash, str(running), mode)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        return running, out.stdout

    def test_receiver_never_starts_falls_back_after_three_trial_boots(self):
        for n in (1, 2, 3):
            running, text = self.boot('dead')
            self.assertEqual(running, 2)
            self.assertIn('OTA: fw2 trial boot %d of 3\n' % n, text)
            self.assertNotIn('listening', text)
            # 試開逾時的計時器回呼：先通知其他模組再重置，不經輸出的鎖
            self.assertIn('result reset\n', text)
            self.assertIn('events BR\n', text)
            self.assertIn('blocked 1\n', text)
        running, text = self.boot('dead')
        self.assertEqual(running, 2)
        self.assertIn('OTA: fw2 not confirmed after 3 trial boots, falling back\n', text)
        self.assertIn('result reset\n', text)
        self.assertIn('events R\n', text)                 # 排程器啟動前：不通知模組
        self.assertEqual(self.labels(), (True, False))   # fw2 的標籤清掉，bootloader 回到 fw1
        running, text = self.boot('dead')
        self.assertEqual(running, 1)
        self.assertNotIn('OTA: fw', text)                 # 舊槽照常開機，沒有試開紀錄
        self.assertIn('result running\n', text)
        self.assertIn('blocked 0\n', text)

    def test_healthy_image_is_confirmed_and_then_boots_normally(self):
        running, text = self.boot('healthy')
        self.assertEqual(running, 2)
        self.assertIn('OTA: fw2 trial boot 1 of 3\n', text)
        self.assertIn('OTA: listening on TCP 8266\n', text)
        self.assertIn('OTA: fw2 confirmed\n', text)
        self.assertIn('blocked 0\n', text)
        for _ in range(2):
            running, text = self.boot('dead')             # 確認之後就算接收端沒起來也不會再試開或退回
            self.assertEqual(running, 2)
            self.assertNotIn('OTA: fw', text)
            self.assertIn('result running\n', text)
        self.assertEqual(self.labels(), (True, True))

    def test_confirmed_after_a_failed_trial_boot(self):
        # 第一次開機接收端沒起來（逾時重置），第二次健康：確認後停在新映像
        running, text = self.boot('dead')
        self.assertIn('OTA: fw2 trial boot 1 of 3\n', text)
        running, text = self.boot('healthy')
        self.assertEqual(running, 2)
        self.assertIn('OTA: fw2 trial boot 2 of 3\n', text)
        self.assertIn('OTA: fw2 confirmed\n', text)
        running, text = self.boot('dead')
        self.assertEqual((running, 'OTA: fw' in text), (2, False))


if __name__ == '__main__':
    unittest.main()
