#!/usr/bin/env python3
# encoding: utf-8
# SPDX-License-Identifier: GPL-3.0-or-later

"""
把 firmware_ntz.bin 簽成經 Wi-Fi 更新（OTA）用的 .ota 檔（標頭版本 2，板上見 libraries/AP_HAL_RTL8735B/app/ota/ota_session.h）。
私鑰只在這一步用；傳送用 rtl8735b_ota_send.py，不需要任何金鑰。

金鑰對用 ArduPilot 的 Tools/scripts/signing/generate_keys.py 產生（PRIVATE_KEYV1:/PUBLIC_KEYV1: 加 base64），
建置韌體時以環境變數 RTL8735B_OTA_PUBLIC_KEY 指向公鑰檔，韌體只帶公鑰。

.ota 檔就是傳送端送給板子的位元組，依序是：
  標頭 16 bytes   little-endian：magic "ROTA"、版本 2、旗標（1：寫完不重開）、映像長度、映像的位元組總和（mod 2^32）
  雜湊 64 bytes   映像的 BLAKE2b-512
  簽章 64 bytes   Monocypher 的 EdDSA（curve25519 + BLAKE2b），簽「標頭 ‖ 雜湊」
  映像            firmware_ntz.bin 的內容
旗標在簽章範圍內：要「寫完不重開」就簽一份帶 --no-reboot 的檔。

需要 pymonocypher 3.1.3.2（和 generate_keys.py、make_secure_fw.py 相同）：
  python3 -m pip install pymonocypher==3.1.3.2

用法：rtl8735b_ota_sign.py --private-key <名稱>_private_key.dat [--no-reboot] firmware_ntz.bin <輸出.ota>

"""

import argparse
import base64
import hashlib
import struct
import sys

HEADER = struct.Struct('<IHHII')
MAGIC = 0x41544F52
VERSION = 2
FLAG_NO_REBOOT = 0x0001
HASH_LEN = 64
SIG_LEN = 64
KEY_LEN = 32
LABEL = b'RTL8735B'
MONOCYPHER_VERSION = '3.1.3.2'


class SignError(Exception):
    pass


def load_image(path):
    '''firmware_ntz.bin（SDK 打包的韌體映像，開頭是標籤 RTL8735B）'''
    try:
        with open(path, 'rb') as f:
            image = f.read()
    except OSError as e:
        raise SignError('讀不到 %s：%s' % (path, e.strerror))
    if not image.startswith(LABEL):
        raise SignError('%s 不是 RTL8735B 的韌體映像（開頭不是 RTL8735B）；要簽的是 sdk_build/firmware_ntz.bin' % path)
    return image


def parse_key(text, kind):
    '''generate_keys.py 的金鑰檔：「<kind>_KEYV1:」加 32 bytes 的 base64；kind 是 PRIVATE 或 PUBLIC'''
    prefix = kind + '_KEYV1:'
    text = text.strip()
    if not text.startswith(prefix):
        other = 'PUBLIC' if kind == 'PRIVATE' else 'PRIVATE'
        if text.startswith(other + '_KEYV1:'):
            raise SignError('這是%s鑰檔，要的是%s鑰檔' % ('公' if other == 'PUBLIC' else '私', '私' if kind == 'PRIVATE' else '公'))
        raise SignError('不是 generate_keys.py 產生的金鑰檔（開頭不是 %s）' % prefix)
    try:
        key = base64.b64decode(text[len(prefix):], validate=True)
    except ValueError:
        raise SignError('金鑰檔的 base64 不正確')
    if len(key) != KEY_LEN:
        raise SignError('金鑰長度是 %d bytes，應該是 %d' % (len(key), KEY_LEN))
    return key


def load_key(path, kind):
    try:
        with open(path, encoding='utf-8') as f:
            return parse_key(f.read(), kind)
    except OSError as e:
        raise SignError('讀不到金鑰檔 %s：%s' % (path, e.strerror))
    except UnicodeDecodeError:
        raise SignError('金鑰檔 %s 不是文字檔' % path)


def monocypher():
    try:
        import monocypher as m
    except ImportError:
        raise SignError('需要 pymonocypher %s：python3 -m pip install pymonocypher==%s'
                        % (MONOCYPHER_VERSION, MONOCYPHER_VERSION))
    if m.__version__ != MONOCYPHER_VERSION:
        raise SignError('pymonocypher 要 %s，裝的是 %s' % (MONOCYPHER_VERSION, m.__version__))
    return m


def signed_message(image, reboot=True):
    '''回傳「標頭 ‖ 雜湊」，也就是要簽的訊息'''
    header = HEADER.pack(MAGIC, VERSION, 0 if reboot else FLAG_NO_REBOOT, len(image), sum(image) & 0xFFFFFFFF)
    return header + hashlib.blake2b(image, digest_size=HASH_LEN).digest()


def sign(image, private_key, reboot=True):
    '''回傳 .ota 檔的內容'''
    m = monocypher()
    msg = signed_message(image, reboot)
    signature = m.signature_sign(private_key, msg)
    if len(signature) != SIG_LEN:
        raise SignError('簽章長度不對：%d' % len(signature))
    return msg + signature + image


def main(argv=None):
    p = argparse.ArgumentParser(description='把 firmware_ntz.bin 簽成 RTL8735B 經 Wi-Fi 更新用的 .ota 檔')
    p.add_argument('--private-key', required=True, help='generate_keys.py 產生的 <名稱>_private_key.dat')
    p.add_argument('--no-reboot', action='store_true', help='板子寫完不重開（旗標在簽章範圍內）')
    p.add_argument('firmware', help='sdk_build/firmware_ntz.bin')
    p.add_argument('output', help='輸出的 .ota 檔')
    a = p.parse_args(argv)
    try:
        image = load_image(a.firmware)
        key = load_key(a.private_key, 'PRIVATE')
        data = sign(image, key, reboot=not a.no_reboot)
        with open(a.output, 'wb') as f:
            f.write(data)
    except SignError as e:
        print('簽章失敗：%s' % e, file=sys.stderr)
        return 1
    except OSError as e:
        print('寫不出 %s：%s' % (a.output, e.strerror), file=sys.stderr)
        return 1
    print('已簽：%s（映像 %d bytes%s）' % (a.output, len(image), '，寫完不重開' if a.no_reboot else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
