#!/usr/bin/env python3
'''libraries/AP_HAL_RTL8735B/app/ota/ota_core.c 的主機測試：編譯 ota_core.c、Monocypher 與 ota_core_test.c（假 NOR Flash），逐案例執行。'''

import tempfile
import unittest

import ota_cc

CASES = [
    'read_slots_ok', 'read_slots_rejects_bad_tables', 'slot_touching_boot_area_rejected',
    'slot_crossing_user_data_rejected', 'slot_in_param_area_rejected', 'slots_at_area_edges_ok', 'begin_limits', 'never_touches_running_slot',
    'running_fw2_targets_fw1', 'label_written_last', 'chunk_sizes_do_not_matter',
    'checksum_mismatch_leaves_slot_invalid', 'rejects_image_without_label', 'write_past_image_len_rejected',
    'range_allowed_edges', 'partial_then_new_session', 'trial_boots_then_rollback',
    'no_fallback_when_other_slot_invalid', 'confirm_ends_trial', 'confirmed_boot_writes_nothing',
    'no_record_means_normal_boot', 'header_parse', 'auth_header',
    'write_before_auth_rejected', 'bad_image_hash_leaves_slot_invalid', 'tampered_image_rejected',
    'abort_ends_session', 'trial_pending_states', 'verify_then_activate', 'confirm_ready_rules',
    'unequal_reversed_slots', 'read_slots_realtek_ota_layout', 'read_slots_rejects_layout_mismatch',
    'read_slots_rejects_invalid_record', 'read_slots_checks_capacity', 'slots_fit_rules',
    'layout_mismatch_trial_falls_back', 'boot_plan_rules', 'capacity_unknown_assumes_16mib', 'trial_timeout_rules',
]


@unittest.skipUnless(ota_cc.CAN_RUN, ota_cc.SKIP_REASON)
class OtaCoreTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = ota_cc.compile_exe(cls.tmp.name, 'ota_core_test', ['ota_core.c'], 'ota_core_test.c')

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
    setattr(OtaCoreTest, 'test_' + _name, lambda self, n=_name: self.run_case(n))


if __name__ == '__main__':
    unittest.main()
