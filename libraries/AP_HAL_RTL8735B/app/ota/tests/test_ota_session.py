#!/usr/bin/env python3
'''libraries/AP_HAL_RTL8735B/app/ota/ota_session.c 的主機測試：簽章、映像被改、逾時、解鎖檢查與「OTA 進行中」旗標。
以假連線、假時鐘、假 Flash 逐案例執行 ota_session_test.c；拒收的案例都核對目前的槽與分割表沒變、重開後仍開原來的槽。'''

import tempfile
import unittest

import ota_cc

CASES = [
    'success', 'progress_every_5_percent', 'success_no_reboot_flag', 'armed_before_reboot', 'flag_set_during_data',
    'armed_at_accept_closes_silently', 'no_key', 'no_key_bad_length', 'header_timeout', 'header_trickle',
    'close_in_header', 'wrong_key', 'header_bit_flipped', 'hash_modified', 'signature_modified',
    'bad_header_after_auth', 'too_large', 'armed_after_auth', 'status_send_fail', 'armed_mid_data',
    'idle_timeout_mid_data', 'close_mid_data', 'flash_error_mid_data', 'total_cap', 'image_byte_tampered',
    'signed_hash_covers_other_length', 'armed_before_label', 'replay_accepted_no_nonce',
    'busy_while_running_on_trial', 'armed_during_readback', 'final_status_send_fail_still_reboots',
    'layout_mismatch', 'probe_confirmed', 'probe_reports_trial_then_confirmed', 'probe_layout_mismatch',
]


@unittest.skipUnless(ota_cc.CAN_RUN, ota_cc.SKIP_REASON)
class OtaSessionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = ota_cc.compile_exe(cls.tmp.name, 'ota_session_test', ['ota_session.c', 'ota_core.c'],
                                     'ota_session_test.c')

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
    setattr(OtaSessionTest, 'test_' + _name, lambda self, n=_name: self.run_case(n))


@unittest.skipUnless(ota_cc.CAN_RUN, ota_cc.SKIP_REASON)
class TcpHarnessBuildTest(unittest.TestCase):
    '''ota_session_tcp.c（tools/tests/test_ota_public.py 的端對端測試用）照樣編得過'''

    def test_builds(self):
        with tempfile.TemporaryDirectory() as tmp:
            ota_cc.compile_exe(tmp, 'ota_session_tcp', ['ota_session.c', 'ota_core.c'], 'ota_session_tcp.c')


if __name__ == '__main__':
    unittest.main()
