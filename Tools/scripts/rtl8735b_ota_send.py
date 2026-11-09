#!/usr/bin/env python3
# encoding: utf-8
# SPDX-License-Identifier: GPL-3.0-or-later

"""
經 Wi-Fi 把簽好的 .ota 檔（rtl8735b_ota_sign.py 產生）送給板子的 OTA 接收端（標頭版本 2，板上見
libraries/AP_HAL_RTL8735B/app/ota/ota_session.h）。不需要任何金鑰：簽章由板上的公鑰驗。

  1. 板子回 4 bytes 狀態；已解鎖的板子什麼都不送就關閉連線。
  2. 送 .ota 檔開頭的 144 bytes：標頭 ‖ 映像的 BLAKE2b-512 ‖ 簽章。
  3. 板子驗過簽章、檢查過才回 0；這之前不碰 Flash。
  4. 送映像。
  5. 板子回最後的狀態，0 表示映像的雜湊和簽過的相同，已寫進另一個韌體槽並啟用。

送之前先在電腦上核對 .ota 檔：格式、長度、雜湊與位元組總和都要和標頭相符，壞掉的檔不送。
給 --public-key 時另外驗簽章（要 pymonocypher 3.1.3.2），確定是那把金鑰簽的再送。

試開探測（--probe）：只問板子目前從哪一槽開機、那一槽還在不在試開；不送映像、不寫 Flash、不重開，也不需要 .ota 檔。
換好映像、板子重開之後，用它確認換好了：回「fw2 已確認」就是新映像已經在跑、而且通過了試開。

用法：
  rtl8735b_ota_send.py --host <板子 IP> [--port 8266] <檔名.ota>
  rtl8735b_ota_send.py --host <板子 IP> [--port 8266] --probe

"""

import argparse
import hashlib
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
import rtl8735b_ota_sign as fmt  # noqa: E402

STATUS = struct.Struct('<I')
REQUEST_LEN = fmt.HEADER.size + fmt.HASH_LEN + fmt.SIG_LEN
PROBE_MAGIC = 0x42525052
DEFAULT_PORT = 8266
CHUNK = 4096
# 板子收完映像後要讀回整個映像核對才回最後的狀態
COMMIT_TIMEOUT = 120.0
STATUS_TEXT = {
    1: 'bad header', 2: 'vehicle armed', 3: 'image too large',
    4: 'busy: the running image is still on trial, wait until it is confirmed', 5: 'partition table',
    6: 'checksum mismatch', 7: 'flash error', 8: 'timeout', 9: 'address out of range', 10: 'bad state',
    11: 'image does not match the signed hash, or is not a firmware image',
    12: 'signature check failed (signed with another key?)', 13: 'no OTA public key in the firmware',
    16: "the board's partition table does not match this firmware's flash layout, "
        'or the flash is too small; update it by manual flashing',
}


class OtaError(Exception):
    pass


def load_ota(path):
    '''讀 .ota 檔並核對（不需金鑰）；回傳（要先送的 144 bytes, 映像, 旗標）'''
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as e:
        raise OtaError('讀不到 %s：%s' % (path, e.strerror))
    if len(data) < REQUEST_LEN + len(fmt.LABEL):
        raise OtaError('%s 太短，不是 rtl8735b_ota_sign.py 簽的 .ota 檔' % path)
    magic, version, flags, length, checksum = fmt.HEADER.unpack_from(data)
    if magic != fmt.MAGIC or version != fmt.VERSION:
        raise OtaError('%s 不是標頭版本 2 的 .ota 檔（magic 或版本不對）' % path)
    image = data[REQUEST_LEN:]
    if length != len(image):
        raise OtaError('%s 的標頭寫映像 %d bytes，檔案裡有 %d bytes' % (path, length, len(image)))
    if sum(image) & 0xFFFFFFFF != checksum:
        raise OtaError('%s 的位元組總和和標頭不符，檔案可能損壞' % path)
    if hashlib.blake2b(image, digest_size=fmt.HASH_LEN).digest() != data[fmt.HEADER.size:fmt.HEADER.size + fmt.HASH_LEN]:
        raise OtaError('%s 的映像和簽過的雜湊不符，檔案可能損壞或被改過' % path)
    if not image.startswith(fmt.LABEL):
        raise OtaError('%s 裡的映像不是 RTL8735B 的韌體（開頭不是 RTL8735B）' % path)
    return data[:REQUEST_LEN], image, flags


def check_signature(request, public_key):
    '''用公鑰驗 .ota 檔的簽章（要 pymonocypher）；不符就丟 OtaError'''
    try:
        m = fmt.monocypher()
    except fmt.SignError as e:
        raise OtaError(str(e))
    msg = request[:fmt.HEADER.size + fmt.HASH_LEN]
    if not m.signature_check(request[len(msg):], public_key, msg):
        raise OtaError('.ota 檔的簽章不是 --public-key 這把金鑰簽的')


def _recv_upto(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            break
        data += chunk
    return data


def _recv_exact(sock, n, stage):
    data = _recv_upto(sock, n)
    if len(data) < n:
        raise OtaError('%s：板子中斷了連線' % stage)
    return data


def _status_error(stage, code):
    return OtaError('%s：板子回報 %d（%s）' % (stage, code, STATUS_TEXT.get(code, 'unknown')))


def _check_status(sock, stage):
    code = STATUS.unpack(_recv_exact(sock, STATUS.size, stage))[0]
    if code != 0:
        raise _status_error(stage, code)


def _try_status(sock):
    '''板子中途中止時會先送狀態再關閉連線；盡量讀出原因'''
    try:
        sock.settimeout(1.0)
        data = _recv_upto(sock, STATUS.size)
    except OSError:
        return None
    return STATUS.unpack(data)[0] if len(data) == STATUS.size else None


def _hello(sock):
    hello = _recv_upto(sock, STATUS.size)
    if not hello:
        raise OtaError('hello：板子沒有回應就關閉了連線（vehicle armed，或拒絕連線）')
    if len(hello) < STATUS.size:
        raise OtaError('hello：板子中斷了連線')
    code = STATUS.unpack(hello)[0]
    if code != 0:
        raise _status_error('hello', code)


def send(host, port, request, image, timeout=30.0, progress=None):
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            _hello(sock)
            sock.sendall(request)
            _check_status(sock, 'signature')
            try:
                for off in range(0, len(image), CHUNK):
                    part = image[off:off + CHUNK]
                    sock.sendall(part)
                    if progress:
                        progress(off + len(part), len(image))
            except OSError:
                code = _try_status(sock)
                if code:
                    raise _status_error('image', code)
                raise
            sock.settimeout(max(timeout, COMMIT_TIMEOUT))
            try:
                code = STATUS.unpack(_recv_exact(sock, STATUS.size, 'commit'))[0]
            except (OtaError, OSError):
                raise OtaError('commit：沒收到最後的狀態（installation state unknown）。映像可能已寫入並重開；'
                               '等板子開機後用 --probe 看它從哪一槽開機，再決定是否重送')
            if code != 0:
                raise _status_error('commit', code)
    except OSError as e:
        raise OtaError('連線 %s:%d 失敗：%s' % (host, port, e))
    return len(image)


def probe(host, port, timeout=5.0):
    '''試開探測：回傳（狀態, 目前開機的槽）。狀態 0 是已確認，4（busy）是還在試開'''
    request = struct.pack('<IH', PROBE_MAGIC, fmt.VERSION).ljust(fmt.HEADER.size, b'\0')
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            _hello(sock)
            sock.sendall(request)
            code, slot = struct.unpack('<II', _recv_exact(sock, 8, 'probe'))
    except OSError as e:
        raise OtaError('試開探測 %s:%d 失敗：%s' % (host, port, e))
    return code, slot


def probe_text(code, slot):
    if code == 0:
        return 'fw%d 已確認（不在試開中）' % slot
    if code == 4:
        return 'fw%d 還在試開、等確認' % slot
    return 'fw%d：板子回報 %d（%s）' % (slot, code, STATUS_TEXT.get(code, 'unknown'))


def _clock_text(t):
    return time.strftime('%H:%M:%S', time.localtime(t)) + '.%03d' % int((t % 1) * 1000)


def progress_printer(out, now=time.time, interval=1.0):
    '''傳送進度。終端機上原地更新一行；導到檔案時每 interval 秒（與最後一塊）記一行電腦時間與已送出的位元組'''
    tty = out.isatty()
    last = [None]

    def show(done, total):
        t = now()
        if tty:
            out.write('\r%d / %d bytes' % (done, total))
        elif last[0] is None or t - last[0] >= interval or done == total:
            out.write('%s %d / %d bytes\n' % (_clock_text(t), done, total))
            last[0] = t
        out.flush()
    return show


def main(argv=None):
    p = argparse.ArgumentParser(description='經 Wi-Fi 把簽好的 .ota 檔送給 RTL8735B 韌體')
    p.add_argument('--host', required=True, help='板子的 IP')
    p.add_argument('--port', type=int, default=DEFAULT_PORT)
    p.add_argument('--public-key', help='送之前先用這把公鑰（generate_keys.py 的 <名稱>_public_key.dat）驗 .ota 檔的簽章')
    p.add_argument('--probe', action='store_true', help='只問目前從哪一槽開機、確認了沒有；不送映像、不寫 Flash')
    p.add_argument('--timeout', type=float, default=30.0)
    p.add_argument('ota', nargs='?', help='rtl8735b_ota_sign.py 簽的 .ota 檔')
    a = p.parse_args(argv)
    if a.probe == bool(a.ota):
        p.error('給 .ota 檔，或只給 --probe')
    try:
        if a.probe:
            code, slot = probe(a.host, a.port, timeout=a.timeout)
            print('試開探測：%s' % probe_text(code, slot))
            return 0 if code in (0, 4) else 1
        request, image, flags = load_ota(a.ota)
        if a.public_key:
            try:
                key = fmt.load_key(a.public_key, 'PUBLIC')
            except fmt.SignError as e:
                raise OtaError(str(e))
            check_signature(request, key)
        n = send(a.host, a.port, request, image, timeout=a.timeout, progress=progress_printer(sys.stdout))
    except OtaError as e:
        print('\nOTA 失敗（%s）：%s' % (_clock_text(time.time()), e), file=sys.stderr)
        return 1
    no_reboot = flags & fmt.FLAG_NO_REBOOT
    print('\nOTA 完成：%d bytes 已寫入另一個韌體槽%s' % (n, '' if no_reboot else '，板子未解鎖時會重開機；開機後可用 --probe 確認'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
