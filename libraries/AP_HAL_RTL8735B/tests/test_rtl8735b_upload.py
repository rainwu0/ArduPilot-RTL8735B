#!/usr/bin/env python3
"""Tools/ardupilotwaf/rtl8735b_upload.py（--upload）的主機測試；不需要板子。"""

import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[3]   # 儲存庫根目錄（tests → AP_HAL_RTL8735B → libraries → 根）
MODULE = ROOT / 'Tools/ardupilotwaf/rtl8735b_upload.py'
sys.path.insert(0, str(MODULE.parent))
import rtl8735b_upload as up  # noqa: E402

TOOL_FILES = ('uartfwburn.linux', 'uartfwburn.exe', 'flash_loader_nor.bin')


def make_sdk(root):
    '''假的 SDK：tools/ 下放一個和真的同名、同目錄結構的燒錄工具 zip'''
    zpath = Path(root) / up.PG_TOOL_ZIP
    zpath.parent.mkdir(parents=True)
    with zipfile.ZipFile(zpath, 'w') as z:
        for name in TOOL_FILES:
            z.writestr(up.PG_TOOL_DIR + '/' + name, b'fake ' + name.encode())
    return str(root)


class FakeTool:
    '''依序回傳預先排好的工具輸出，並記錄每次的指令與工作目錄'''

    def __init__(self, outputs):
        self.outputs = list(outputs)
        self.calls = []

    def __call__(self, cmd, cwd, timeout):
        self.calls.append((cmd, cwd, timeout))
        return self.outputs.pop(0)


class FakeClock:
    def __init__(self):
        self.t = 0.0
        self.sleeps = []

    def now(self):
        return self.t

    def sleep(self, s):
        self.sleeps.append(s)
        self.t += s


class PortTests(unittest.TestCase):
    def test_port_kind(self):
        self.assertEqual(up.port_kind('COM7'), 'windows')
        self.assertEqual(up.port_kind('com12'), 'windows')
        self.assertEqual(up.port_kind('/dev/ttyUSB0'), 'posix')
        for bad in ('ttyUSB0', 'COM', 'COM7x', ''):
            with self.assertRaises(up.UploadError):
                up.port_kind(bad)

    def test_check_static(self):
        with self.assertRaises(up.UploadError):
            up.check_static(None, None, lambda: True)
        up.check_static(None, 'echo', lambda: False)
        with self.assertRaises(up.UploadError) as cm:
            up.check_static('COM7', None, lambda: False)
        self.assertIn('WSLInterop', str(cm.exception))
        up.check_static('COM7', None, lambda: True)
        up.check_static('/dev/ttyUSB0', None, lambda: False)

    def test_interop_detection(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertFalse(up.wsl_interop_available(d))
            Path(d, 'WSLInterop-late').write_text('enabled')
            self.assertTrue(up.wsl_interop_available(d))


class CommandTests(unittest.TestCase):
    def test_tool_command(self):
        for kind, port in (('windows', 'COM7'), ('posix', '/dev/ttyUSB0')):
            cmd = up.tool_command(kind, port)
            self.assertEqual(cmd[0], './' + up.TOOL_EXE[kind])
            self.assertEqual(cmd[1:], ['-p', port, '-f', 'flash_ntz.bin', '-b', '2000000', '-U'])
            self.assertNotIn('-e', cmd)
            self.assertNotIn('-r', cmd)

    def test_override_command_matches_chibios(self):
        self.assertEqual(up.override_command('flash.sh', '/b/flash_ntz.bin', None), "flash.sh '/b/flash_ntz.bin'")
        self.assertEqual(up.override_command('flash.sh', '/b/flash_ntz.bin', 'COM7'),
                         "flash.sh '/b/flash_ntz.bin' '--port' 'COM7'")

    def test_classify(self):
        self.assertEqual(up.classify('... download success\n'), 'success')
        for text in ('ping fail', 'Uart boot fail', 'UCFG FAIL'):
            self.assertEqual(up.classify('x\r' + text + '\n'), 'retry')
        self.assertEqual(up.classify('Create File Error\nuart COM7 open fail'), 'busy')
        self.assertEqual(up.classify('something else'), 'fail')


class StageTests(unittest.TestCase):
    def test_stage_tool_into_path_with_space(self):
        with tempfile.TemporaryDirectory() as d:
            sdk = make_sdk(Path(d, 'sdk'))
            dest = Path(d, 'out dir')
            dest.mkdir()
            tool_dir = up.stage_tool(sdk, str(dest))
            self.assertEqual(Path(tool_dir).name, up.PG_TOOL_DIR)
            for name in TOOL_FILES:
                self.assertTrue(Path(tool_dir, name).is_file())
            self.assertTrue(os.access(Path(tool_dir, 'uartfwburn.linux'), os.X_OK))
            Path(tool_dir, 'stale').write_text('x')
            up.stage_tool(sdk, str(dest))
            self.assertFalse(Path(tool_dir, 'stale').exists())

    def test_stage_tool_missing_zip(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(up.UploadError):
                up.stage_tool(d, d)

    @unittest.skipUnless(os.environ.get('AMEBAPRO2_SDK'), '需要真實 SDK（AMEBAPRO2_SDK）')
    def test_real_sdk_tool_runs(self):
        with tempfile.TemporaryDirectory() as d:
            tool_dir = up.stage_tool(os.environ['AMEBAPRO2_SDK'], d)
            out = subprocess.run(['./uartfwburn.linux', '-h'], cwd=tool_dir, capture_output=True, text=True,
                                 errors='replace').stdout
            self.assertIn('PG Tool 1.3.0', out)
            self.assertIn('-U', out)


class StreamTests(unittest.TestCase):
    def test_stream_passes_carriage_return_output(self):
        code = "import sys; sys.stdout.write('10%\\r20%\\r'); sys.stdout.flush(); print('download success')"
        buf = io.StringIO()
        rc, out = up.stream([sys.executable, '-c', code], None, 30, out=buf)
        self.assertEqual(rc, 0)
        self.assertIn('download success', out)
        self.assertEqual(buf.getvalue(), out)

    def test_stream_timeout(self):
        with self.assertRaises(up.UploadError):
            up.stream([sys.executable, '-c', 'import time; time.sleep(30)'], None, 1, out=io.StringIO())


class UploadTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.sdk = make_sdk(root / 'sdk')
        self.image = root / 'flash_ntz.bin'
        self.image.write_bytes(b'image')
        self.device = root / 'ttyFAKE'
        self.device.write_bytes(b'')
        self.work = str(root / 'pg tool')
        self.clock = FakeClock()
        self.logs = []

    def tearDown(self):
        self.tmp.cleanup()

    def posix(self, tool, **kw):
        # check_device 只看路徑存在與讀寫權限：用暫存檔代替序列裝置
        orig = up.port_kind
        up.port_kind = lambda p: 'posix'
        try:
            return up.upload(str(self.image), str(self.device), self.sdk, self.work,
                             run_tool=tool, sleep=self.clock.sleep, clock=self.clock.now,
                             log=self.logs.append, **kw)
        finally:
            up.port_kind = orig

    def test_retry_then_success(self):
        tool = FakeTool([(1, 'ping fail'), (1, 'Uart boot fail'), (0, 'download success')])
        self.posix(tool)
        self.assertEqual(len(tool.calls), 3)
        self.assertEqual(self.clock.sleeps, [up.RETRY_DELAY_S] * 2)
        cmd, cwd, timeout = tool.calls[0]
        self.assertEqual(cmd[0], './uartfwburn.linux')
        self.assertEqual(Path(cwd).name, up.PG_TOOL_DIR)
        self.assertEqual(Path(cwd, 'flash_ntz.bin').read_bytes(), b'image')
        self.assertEqual(timeout, up.ATTEMPT_TIMEOUT_S)

    def test_retry_gives_up_after_window(self):
        tool = FakeTool([(1, 'ping fail')] * 100)
        with self.assertRaises(up.UploadError) as cm:
            self.posix(tool)
        self.assertIn('沒有寫入', str(cm.exception))
        self.assertLessEqual(sum(self.clock.sleeps), up.RETRY_WINDOW_S)

    def test_busy_and_fail_do_not_retry(self):
        for output in ('Create File Error', 'weird error'):
            tool = FakeTool([(1, output)])
            with self.assertRaises(up.UploadError):
                self.posix(tool)
            self.assertEqual(len(tool.calls), 1)

    def test_missing_device(self):
        with self.assertRaises(up.UploadError) as cm:
            up.upload(str(self.image), '/dev/ttyRTL8735B_NOPE', self.sdk, self.work,
                      run_tool=FakeTool([]), log=self.logs.append)
        self.assertIn('usbipd', str(cm.exception))

    def test_windows_port_stages_on_windows_side(self):
        stage = str(Path(self.tmp.name, 'win temp', up.STAGE_NAME))
        tool = FakeTool([(0, 'download success')])
        up.upload(str(self.image), 'COM7', self.sdk, self.work, run_tool=tool, log=self.logs.append,
                  stage_dir=lambda: stage, interop_available=lambda: True)
        cmd, cwd, _ = tool.calls[0]
        self.assertEqual(cmd[:3], ['./uartfwburn.exe', '-p', 'COM7'])
        self.assertTrue(cwd.startswith(stage))
        self.assertTrue(Path(cwd, 'flash_ntz.bin').is_file())

    def test_windows_port_without_interop(self):
        with self.assertRaises(up.UploadError):
            up.upload(str(self.image), 'COM7', self.sdk, self.work, run_tool=FakeTool([]),
                      log=self.logs.append, interop_available=lambda: False)

    def test_override(self):
        seen = []
        up.upload(str(self.image), 'COM7', self.sdk, self.work, override='flash.sh',
                  run_tool=FakeTool([]), shell=lambda c: seen.append(c) or 0, log=self.logs.append)
        self.assertEqual(seen, ["flash.sh '%s' '--port' 'COM7'" % self.image])
        with self.assertRaises(up.UploadError):
            up.upload(str(self.image), None, self.sdk, self.work, override='false',
                      run_tool=FakeTool([]), shell=lambda c: 1, log=self.logs.append)


if __name__ == '__main__':
    unittest.main()
