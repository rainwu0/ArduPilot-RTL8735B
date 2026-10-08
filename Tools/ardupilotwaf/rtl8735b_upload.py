# encoding: utf-8
# SPDX-License-Identifier: GPL-3.0-or-later

"""
RTL8735B 的 --upload：以 SDK 附的 uartfwburn，經 UART 下載模式燒錄 flash_ntz.bin。
本模組不依賴 waf，waf 端的接法在 rtl8735b.py。

- --upload-port COMn：在 WSL 經 interop 執行 Windows 版 uartfwburn.exe。
- --upload-port /dev/ttyXXX：執行 Linux 版 uartfwburn.linux（原生 Linux，或 WSL 以 usbipd 掛上的 USB 序列裝置）。
- 環境變數 AP_OVERRIDE_UPLOAD_CMD：改執行自訂指令，參數與 ChibiOS 相同。

晶片只能以按鍵進入下載模式（uartfwburn 的 -d 不支援本晶片），所以板子不在下載模式時會重試一段時間。
只下載、不抹除：任何情況都不傳 -e。
"""

import glob
import os
import re
import select
import shutil
import subprocess
import sys
import time
import zipfile

PG_TOOL_ZIP = os.path.join('tools', 'Pro2_PG_tool _v1.3.0.zip')
PG_TOOL_DIR = 'Pro2_PG_tool _v1.3.0'
TOOL_EXE = {'windows': 'uartfwburn.exe', 'posix': 'uartfwburn.linux'}
IMAGE_NAME = 'flash_ntz.bin'
BAUD = 2000000
STAGE_NAME = 'ardupilot-rtl8735b-upload'

# 板子不在下載模式時的輸出：重試，讓使用者有時間按鍵
RETRY_MARKERS = ('ping fail', 'uart boot fail', 'ucfg fail')
SUCCESS_MARKER = 'download success'
# Windows 的 COM 埠被其他程式佔用時的輸出
BUSY_MARKERS = ('create file error', 'open fail')

RETRY_WINDOW_S = 60
RETRY_DELAY_S = 3
# 一次完整下載約 46 秒；超過這個時間沒有結束就視為卡住，不重試
ATTEMPT_TIMEOUT_S = 180

INTEROP_HINT = ('--upload-port COMn 要從 WSL 執行 Windows 版 uartfwburn.exe，但目前的 WSL 沒有 interop'
                '（/proc/sys/fs/binfmt_misc 下沒有 WSLInterop）。開了 systemd 的 WSL 在發行版閒置關閉、'
                '又在同一個 WSL VM 內重新啟動後，interop 可能不會重新註冊。在 Windows 執行 wsl --shutdown '
                '讓 WSL 重新開機，並在建置與燒錄期間保持一個 WSL 終端開著；或改用 Linux 的序列裝置 /dev/ttyXXX。')
# HUB 8735 ultra 的按鍵順序（HUB 8735 ultra User Manual【AIoT Starter Kit for Arduino】20240530 p.15）；其他開發板依各自的說明
DOWNLOAD_MODE_KEYS = '例如 HUB 8735 ultra：按住 upgrade 鍵，短按 reset，再放開 upgrade'


class UploadError(Exception):
    '''燒錄條件不成立或燒錄失敗；訊息直接給使用者看'''


def port_kind(port):
    '''COMn 回傳 windows，/dev/ 開頭回傳 posix'''
    if re.fullmatch(r'COM[0-9]+', port, re.IGNORECASE):
        return 'windows'
    if port.startswith('/dev/'):
        return 'posix'
    raise UploadError('無法辨識的燒錄埠 %r：Windows 序列埠用 COMn，Linux 用 /dev/ttyXXX' % port)


def wsl_interop_available(binfmt_dir='/proc/sys/fs/binfmt_misc'):
    return bool(glob.glob(os.path.join(binfmt_dir, 'WSLInterop*')))


def check_static(port, override, interop_available=wsl_interop_available):
    '''建置開始前就能判斷的錯誤；序列裝置是否存在留到燒錄時再查，板子可能建置期間才接上'''
    if override:
        return
    if not port:
        raise UploadError('--upload 需要 --upload-port（COMn 或 /dev/ttyXXX），'
                          '或以環境變數 AP_OVERRIDE_UPLOAD_CMD 指定燒錄指令')
    if port_kind(port) == 'windows' and not interop_available():
        raise UploadError(INTEROP_HINT)


def check_device(port):
    if not os.path.exists(port):
        raise UploadError('找不到 %s：確認 USB 序列線已接上；WSL 要先以 usbipd 把 USB 序列裝置掛進 WSL，'
                          '並載入驅動（例如 sudo modprobe ch341）' % port)
    if not os.access(port, os.R_OK | os.W_OK):
        raise UploadError('沒有 %s 的讀寫權限：把使用者加入 dialout 群組後重新登入' % port)


def tool_command(kind, port):
    '''在工具目錄執行；映像先複製到同一目錄，-f 用相對路徑，避免路徑轉換與空白的問題'''
    return ['./' + TOOL_EXE[kind], '-p', port, '-f', IMAGE_NAME, '-b', str(BAUD), '-U']


def override_command(override, image, port):
    '''與 ChibiOS 的 AP_OVERRIDE_UPLOAD_CMD 相同：指令後接映像路徑，有 --upload-port 時再接 --port'''
    cmd = "{} '{}'".format(override, image)
    if port is not None:
        cmd += " '--port' '%s'" % port
    return cmd


def classify(output):
    '''依工具輸出判斷結果：success、retry（不在下載模式）、busy（埠被佔用）或 fail'''
    low = output.lower()
    if SUCCESS_MARKER in low:
        return 'success'
    if any(m in low for m in RETRY_MARKERS):
        return 'retry'
    if any(m in low for m in BUSY_MARKERS):
        return 'busy'
    return 'fail'


def stage_tool(sdk, dest):
    '''把 SDK 的燒錄工具解到 dest；zipfile 不保留執行位元，解完補上'''
    zpath = os.path.join(sdk, PG_TOOL_ZIP)
    if not os.path.isfile(zpath):
        raise UploadError('找不到 SDK 的燒錄工具：%s' % zpath)
    tool_dir = os.path.join(dest, PG_TOOL_DIR)
    shutil.rmtree(tool_dir, ignore_errors=True)
    with zipfile.ZipFile(zpath) as z:
        z.extractall(dest)
    for exe in TOOL_EXE.values():
        p = os.path.join(tool_dir, exe)
        if os.path.exists(p):
            os.chmod(p, os.stat(p).st_mode | 0o111)
    return tool_dir


def windows_stage_dir(run=subprocess.run):
    '''Windows 的 %TEMP% 下的暫存目錄（WSL 路徑）。Windows 程式從 WSL 檔案系統執行的行為未驗證，
    所以 COM 埠一律在 Windows 磁碟上執行；cmd.exe 從 /mnt/c 啟動，避免 UNC 工作目錄的警告'''
    temp = run(['cmd.exe', '/c', 'echo %TEMP%'], cwd='/mnt/c',
               capture_output=True, text=True, check=True).stdout.strip()
    path = run(['wslpath', '-u', temp], capture_output=True, text=True, check=True).stdout.strip()
    return os.path.join(path, STAGE_NAME)


def stream(cmd, cwd, timeout, out=None):
    '''執行燒錄工具，輸出即時轉印並回傳 (結束碼, 輸出)。工具以 \\r 更新進度，所以按區塊讀、不按行讀'''
    out = out or sys.stdout
    buf = bytearray()
    deadline = time.monotonic() + timeout
    with subprocess.Popen(cmd, cwd=cwd, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        fd = proc.stdout.fileno()
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                proc.kill()
                raise UploadError('燒錄工具超過 %d 秒沒有結束，已中止' % timeout)
            ready, _, _ = select.select([fd], [], [], min(remaining, 1.0))
            if not ready:
                continue
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            buf += chunk
            out.write(chunk.decode('utf-8', 'replace'))
            out.flush()
    return proc.returncode, buf.decode('utf-8', 'replace')


def _log(msg):
    # 立即輸出，才不會排在燒錄工具或自訂指令的輸出之後
    print(msg, flush=True)


def upload(image, port, sdk, workdir, override=None, *,
           run_tool=stream, shell=None, sleep=time.sleep, clock=time.monotonic, log=_log,
           stage_dir=windows_stage_dir, interop_available=wsl_interop_available):
    '''燒錄 image；失敗時丟出 UploadError。後面幾個參數讓主機測試替換'''
    if override:
        cmd = override_command(override, image, port)
        log('RTL8735B 燒錄：' + cmd)
        rc = (shell or (lambda c: subprocess.call(c, shell=True)))(cmd)
        if rc != 0:
            raise UploadError('AP_OVERRIDE_UPLOAD_CMD 失敗（結束碼 %d）' % rc)
        return

    check_static(port, override, interop_available)
    kind = port_kind(port)
    if kind == 'posix':
        check_device(port)
        stage = workdir
    else:
        stage = stage_dir()
    os.makedirs(stage, exist_ok=True)
    tool_dir = stage_tool(sdk, stage)
    shutil.copyfile(image, os.path.join(tool_dir, IMAGE_NAME))

    cmd = tool_command(kind, port)
    log('RTL8735B 燒錄：%s（工作目錄 %s）' % (' '.join(cmd), tool_dir))
    log('請讓板子進入燒錄模式（%s）；%d 秒內沒有進入會自動重試' % (DOWNLOAD_MODE_KEYS, RETRY_WINDOW_S))
    deadline = clock() + RETRY_WINDOW_S
    while True:
        rc, output = run_tool(cmd, tool_dir, ATTEMPT_TIMEOUT_S)
        result = classify(output)
        if result == 'success':
            log('RTL8735B 燒錄完成；重置板子後開機')
            return
        if result == 'busy':
            raise UploadError('%s 被其他程式佔用（例如地面站軟體），關閉後再試' % port)
        if result == 'retry':
            if clock() + RETRY_DELAY_S < deadline:
                log('板子還沒進入燒錄模式，%d 秒後重試' % RETRY_DELAY_S)
                sleep(RETRY_DELAY_S)
                continue
            raise UploadError('%d 秒內板子沒有進入燒錄模式，沒有寫入' % RETRY_WINDOW_S)
        raise UploadError('燒錄失敗（結束碼 %d），工具輸出見上方' % rc)
