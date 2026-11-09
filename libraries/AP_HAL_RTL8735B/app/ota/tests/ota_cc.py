#!/usr/bin/env python3
'''OTA（libraries/AP_HAL_RTL8735B/app/ota/）主機測試共用：以本機 C 編譯器（AddressSanitizer、UBSan）編譯 C 檔與測試程式。
簽章與雜湊用本庫的 Monocypher（libraries/AP_CheckFirmware/monocypher.cpp），和板上用的是同一份；
ota_monocypher.c 以相對路徑引入它，所以直接在原位編譯，不複製、也不寫進原始碼目錄。
'''

import os
from pathlib import Path
import shutil
import subprocess

TESTS = Path(__file__).resolve().parent
OTA = TESTS.parent
ROOT = Path(__file__).resolve().parents[5]
CC = shutil.which('cc') or shutil.which('gcc')
_MONO = ROOT / 'libraries/AP_CheckFirmware/monocypher'

SKIP_REASON = '需要 POSIX 的 C 編譯器與本庫的 libraries/AP_CheckFirmware/monocypher.{h,cpp}'

CAN_RUN = bool(CC and os.name == 'posix' and _MONO.with_suffix('.cpp').is_file() and _MONO.with_suffix('.h').is_file())

SANITIZE = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all']


def compile_exe(tmpdir, name, ota_sources, test_c, include_dirs=(), defines=()):
    '''編譯並回傳執行檔路徑。ota_monocypher.c（只引入 Monocypher）不加 -Werror（不是本庫的程式），本庫與測試程式加。
    include_dirs 放假的 SDK 標頭，排在應用程式目錄之前；defines 是只加在本庫與測試程式的 -D。
    app/ 的 rtl8735b_app.h 是真的標頭，一併加進 -I（假的 SDK 標頭目錄裡沒有同名檔）'''
    tmp = Path(tmpdir)
    app = OTA
    common = ['-std=c99', *SANITIZE]
    obj = tmp / 'ota_monocypher.o'
    _run_cc([CC, *common, '-w', '-c', str(app / 'ota_monocypher.c'), '-o', str(obj)])
    exe = str(tmp / name)
    extra = [arg for d in include_dirs for arg in ('-I', str(d))]
    _run_cc([CC, *common, '-Wall', '-Wextra', '-Werror', *defines, *extra, '-I', str(app), '-I', str(app.parent),
             *[str(app / s) for s in ota_sources], str(TESTS / test_c), str(obj), '-o', exe])
    return exe


def _run_cc(cmd):
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        raise AssertionError('編譯失敗：\n' + ' '.join(cmd) + '\n' + out.stdout + out.stderr)


def run(exe, *args, stdin=None, timeout=120):
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    return subprocess.run([exe, *args], capture_output=True, text=True, env=env, timeout=timeout, input=stdin)
