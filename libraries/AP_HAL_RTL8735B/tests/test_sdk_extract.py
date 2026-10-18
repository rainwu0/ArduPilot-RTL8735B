#!/usr/bin/env python3
"""targets/cmake/sdk_extract.py 的主機測試。"""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]   # 儲存庫根目錄（tests → AP_HAL_RTL8735B → libraries → 根）
TOOL = ROOT / 'libraries/AP_HAL_RTL8735B/targets/cmake/sdk_extract.py'
sys.path.insert(0, str(TOOL.parent))
import sdk_extract as se  # noqa: E402

TEXT = '''#MBED
list(
    APPEND out_sources
\t${sdk_root}/a.c  # 註解裡的 ) 不算
)
if(DEFINED EXAMPLE AND EXAMPLE)
add_custom_target(
\tflash_nn
\tCOMMAND x
)
add_custom_target(
\tflash
\tCOMMAND $<TARGET_FILE:${app}> (nested)
\tCOMMAND ${CMAKE_OBJEDUMP} -d foo
)
'''


class ExtractTests(unittest.TestCase):
    def test_region_stops_before_end_marker(self):
        out = se.region(TEXT, '#MBED', 'if(DEFINED EXAMPLE AND EXAMPLE)')
        self.assertTrue(out.startswith('#MBED'))
        self.assertNotIn('EXAMPLE', out)

    def test_region_missing_marker_fails(self):
        with self.assertRaises(LookupError):
            se.region(TEXT, '#NOPE', 'if(DEFINED EXAMPLE')

    def test_block_respects_word_boundary_and_nesting(self):
        out = se.block(TEXT, ['add_custom_target(', 'flash'])
        self.assertIn('(nested)', out)
        self.assertNotIn('flash_nn', out)
        self.assertTrue(out.rstrip().endswith(')'))

    def test_block_missing_fails(self):
        with self.assertRaises(LookupError):
            se.block(TEXT, ['add_custom_target(', 'plain_img'])

    def test_drop_removes_matching_lines(self):
        out = se.drop(se.block(TEXT, ['add_custom_target(', 'flash']), ['OBJEDUMP'])
        self.assertNotIn('OBJEDUMP', out)

    def test_block_ambiguous_fails(self):
        twice = TEXT + 'add_custom_target(\n\tflash\n\tCOMMAND other\n)\n'
        with self.assertRaisesRegex(LookupError, '2'):
            se.block(twice, ['add_custom_target(', 'flash'])

    def test_block_require_selects_one(self):
        twice = TEXT + 'add_custom_target(\n\tflash\n\tCOMMAND other\n)\n'
        out = se.block(twice, ['add_custom_target(', 'flash'], require='COMMAND other')
        self.assertIn('COMMAND other', out)

    def test_cli_within_limits_search(self):
        twice = TEXT + 'if(UNITEST)\nadd_custom_target(\n\tflash\n\tCOMMAND other\n)\n'
        with tempfile.NamedTemporaryFile('w', suffix='.cmake', delete=False, encoding='utf-8') as f:
            f.write(twice)
        try:
            r = subprocess.run([sys.executable, str(TOOL), f.name, '--within', '#MBED', 'if(UNITEST)',
                                '--block', 'add_custom_target(', 'flash'], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn('(nested)', r.stdout)
            self.assertNotIn('COMMAND other', r.stdout)
        finally:
            os.unlink(f.name)

    def test_hash_inside_quotes_is_not_comment(self):
        # 引號內的 # 不是註解；真正的右括號就在同一列的引號後面
        text = 'add_custom_target(quoted COMMAND echo "x#y")\nadd_custom_target(next)\n'
        self.assertEqual(se.block(text, ['add_custom_target(', 'quoted']),
                         'add_custom_target(quoted COMMAND echo "x#y")\n')

    def test_cli_exit_code_on_missing(self):
        with tempfile.NamedTemporaryFile('w', suffix='.cmake', delete=False, encoding='utf-8') as f:
            f.write(TEXT)
        try:
            r = subprocess.run([sys.executable, str(TOOL), f.name, '--block', 'add_custom_target(', 'plain_img'],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 1)
            self.assertIn('plain_img', r.stderr)
        finally:
            os.unlink(f.name)


REQUIRED = ('tasks.c', 'port.c', 'sockets.c', 'libc_wrap.c', 'flash_api.c')


@unittest.skipUnless(shutil.which('cmake'), '需要 cmake')
class SdkImportTests(unittest.TestCase):
    '''以 cmake -P 執行 sdk_import.cmake 的 rtl8735b_sdk_sources'''

    def run_sources(self, names):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / 'p/GCC-RELEASE/application'
            app.mkdir(parents=True)
            body = ''.join(f'  ${{sdk_root}}/x/{n}\n' for n in names)
            (app / 'application.cmake').write_text(
                f'#MBED\nlist(APPEND out_sources\n{body})\nif(DEFINED EXAMPLE AND EXAMPLE)\n', encoding='utf-8')
            script = Path(tmp) / 't.cmake'
            script.write_text(f'set(sdk_root /sdk)\nset(prj_root {tmp}/p)\n'
                              f'include({TOOL.parent}/sdk_import.cmake)\n'
                              'rtl8735b_sdk_sources(srcs)\nmessage(STATUS "SRCS=${srcs}")\n', encoding='utf-8')
            return subprocess.run(['cmake', '-P', str(script)], capture_output=True, text=True)

    def test_all_required_sources_pass(self):
        r = self.run_sources(REQUIRED + ('extra.c',))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('/sdk/x/extra.c', r.stdout + r.stderr)

    def test_missing_required_source_fails(self):
        r = self.run_sources(tuple(n for n in REQUIRED if n != 'sockets.c'))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('sockets.c', r.stdout + r.stderr)

    def test_ethernet_api_excluded(self):
        # SDK 的 ethernet_api.c 是 GPL-2.0-only（檔頭第 14–16 行），本 HAL 不用乙太網路，不編進映像
        r = self.run_sources(REQUIRED + ('ethernet_api.c', 'extra.c'))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('/sdk/x/extra.c', r.stdout + r.stderr)
        self.assertNotIn('ethernet_api.c', r.stdout + r.stderr)


@unittest.skipUnless(os.environ.get('AMEBAPRO2_SDK'), '需要 AMEBAPRO2_SDK')
class RealSdkTests(unittest.TestCase):
    def setUp(self):
        self.gcc = Path(os.environ['AMEBAPRO2_SDK']) / 'project/realtek_amebapro2_v0_example/GCC-RELEASE'

    def test_source_region_present(self):
        text = (self.gcc / 'application/application.cmake').read_text(encoding='latin-1')
        out = se.region(text, '#MBED', 'if(DEFINED EXAMPLE AND EXAMPLE)')
        self.assertIn('APPEND out_sources', out)
        self.assertIn('APPEND app_sources', out)

    def test_packaging_blocks_unique_in_mpchip_branch(self):
        top = (self.gcc / 'CMakeLists.txt').read_text(encoding='latin-1')
        with self.assertRaises(LookupError):  # flash 目標在 MPCHIP 與 testchip 分支各有一個
            se.block(top, ['add_custom_target(', 'flash'])
        mp = se.region(top, 'if(MPCHIP)', 'if(UNITEST)')
        for tokens in (['add_custom_command(', 'OUTPUT', 'key_public.json'],
                       ['add_custom_target(', 'plain_img'],
                       ['add_custom_target(', 'flash']):
            self.assertTrue(se.block(mp, tokens).startswith('add_custom'))
        self.assertIn('PT_FCSDATA', se.block(mp, ['add_custom_target(', 'flash']))

    def test_firmware_post_build_selected(self):
        app = (self.gcc / 'application/CMakeLists.txt').read_text(encoding='latin-1')
        tokens = ['add_custom_command(', 'TARGET', '${app}', 'POST_BUILD']
        with self.assertRaises(LookupError):  # MPCHIP 與 testchip 各一個
            se.block(app, tokens)
        out = se.block(app, tokens, require='FIRMWARE ..firmware_name')
        self.assertNotIn('keygen', out)


if __name__ == '__main__':
    unittest.main()
