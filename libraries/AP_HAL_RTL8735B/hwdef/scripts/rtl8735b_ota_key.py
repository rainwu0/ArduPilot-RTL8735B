#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
'''
由 OTA 公鑰檔產生 ota_public_key.c，放在建置目錄，給開發板應用程式 app/ota 驗韌體更新的簽章（app/ota/ota_app.h）。
公鑰檔是 ArduPilot 的 Tools/scripts/signing/generate_keys.py 產生的 *_public_key.dat：
「PUBLIC_KEYV1:」加 32 bytes 的 base64。沒指定時產生長度 0 的版本，接收端一律拒收。
私鑰檔（PRIVATE_KEYV1）一律拒絕，錯誤訊息不含檔案內容。只在內容改變時覆寫輸出檔，避免無謂的重新連結。

AP_FLAKE8_CLEAN
'''

import argparse
import base64
import binascii
import os
import sys

KEY_LEN = 32           # Monocypher 的 EdDSA 公鑰（libraries/AP_CheckFirmware/monocypher.h，crypto_check）
PREFIX = 'PUBLIC_KEYV1:'


class OtaKeyError(Exception):
    pass


def parse(text):
    '''公鑰檔的文字 → 32 bytes'''
    text = text.strip()
    if text.startswith('PRIVATE_KEYV1:'):
        raise OtaKeyError('這是私鑰檔：建置只用公鑰（*_public_key.dat），私鑰只給簽章工具')
    if not text.startswith(PREFIX):
        raise OtaKeyError('不是 generate_keys.py 產生的公鑰檔（開頭不是 %s）' % PREFIX)
    try:
        key = base64.b64decode(text[len(PREFIX):], validate=True)
    except (binascii.Error, ValueError):
        raise OtaKeyError('公鑰檔的 base64 不正確')
    if len(key) != KEY_LEN:
        raise OtaKeyError('公鑰長度是 %d bytes，應該是 %d' % (len(key), KEY_LEN))
    return key


def render(key):
    body = ', '.join('0x%02x' % b for b in key + bytes(KEY_LEN - len(key)))
    return ('/* 由 AP_HAL_RTL8735B/hwdef/scripts/rtl8735b_ota_key.py 產生：OTA 驗簽章用的公鑰（app/ota/ota_app.h）。 */\n'
            '#include <stdint.h>\n\n'
            'const uint8_t rtl8735b_ota_public_key[%d] = {%s};\n' % (KEY_LEN, body)
            + 'const uint32_t rtl8735b_ota_public_key_len = %d;\n' % len(key))


def generate(key_path, out_path):
    '''產生 out_path；回傳 'configured' 或 'none'。key_path 指定了卻讀不到或格式錯誤時丟出 OtaKeyError，不寫輸出檔'''
    if key_path is None:
        key, status = b'', 'none'
    else:
        try:
            with open(str(key_path), encoding='utf-8') as f:
                text = f.read()
        except (OSError, UnicodeDecodeError) as e:
            raise OtaKeyError('讀不到公鑰檔：%s' % (e.strerror if isinstance(e, OSError) else '不是 UTF-8'))
        key, status = parse(text), 'configured'
    content = render(key)
    out_path = str(out_path)
    try:
        with open(out_path, 'r', encoding='utf-8') as f:
            if f.read() == content:
                return status
    except OSError:
        pass
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    tmp = out_path + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write(content)
    os.replace(tmp, out_path)
    return status


if __name__ == '__main__':
    parser = argparse.ArgumentParser('rtl8735b_ota_key.py')
    parser.add_argument('--key', default=None, help='generate_keys.py 產生的公鑰檔；省略時長度 0，接收端一律拒收')
    parser.add_argument('out', help='輸出的 C 原始碼路徑')
    args = parser.parse_args()
    try:
        print('OTA 公鑰：%s' % generate(args.key, args.out))
    except OtaKeyError as e:
        print('OTA 公鑰錯誤：%s' % e, file=sys.stderr)
        sys.exit(1)
