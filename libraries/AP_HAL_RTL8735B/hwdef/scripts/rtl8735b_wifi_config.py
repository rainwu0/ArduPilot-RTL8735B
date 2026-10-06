#!/usr/bin/env python3
'''
由本機、不進版本管理的 Wi-Fi STA 設定檔產生 wifi_credentials.c，放在建置目錄。
AP_HAL_RTL8735B 的 WiFiDriver 讀取其中的位元組陣列；檔案未指定或不存在時產生 SSID 長度 0 的版本，
建置照常成功、執行時 Wi-Fi 不啟用。設定檔格式（UTF-8，可有 BOM）：

    # 註解與空行略過；等號後到行尾（不含換行）的內容原樣使用，不去除空白
    ssid=<1–32 bytes>
    password=<8–63 個可列印 ASCII 字元，或 64 個十六進位字元；省略或空白表示開放網路>

格式錯誤時丟出 WifiConfigError，訊息不含密碼內容。只在內容改變時覆寫輸出檔，避免無謂的重新連結。

AP_FLAKE8_CLEAN
'''

import argparse
import os
import string
import sys

SSID_MAX = 32          # rtw_ssid_t.val[33]，SDK component/wifi/driver/include/wifi_structures.h
PASSWORD_MIN = 8       # RTW_MIN_PSK_LEN，SDK wifi_constants.h
PASSWORD_MAX = 64      # RTW_WPA2_MAX_PSK_LEN，SDK wifi_constants.h；64 字元時為十六進位 PSK
KEYS = ('ssid', 'password')


class WifiConfigError(Exception):
    pass


def parse(data):
    '''回傳 (ssid bytes, password bytes)；data 為設定檔的原始位元組'''
    try:
        text = data.decode('utf-8')
    except UnicodeDecodeError:
        raise WifiConfigError('Wi-Fi 設定檔必須是 UTF-8')
    if text.startswith('﻿'):
        text = text[1:]
    values = {}
    for number, line in enumerate(text.splitlines(), 1):
        if line.strip() == '' or line.lstrip().startswith('#'):
            continue
        if '=' not in line:
            raise WifiConfigError('Wi-Fi 設定檔第 %d 行缺少等號' % number)
        key, value = line.split('=', 1)
        key = key.strip()
        if key not in KEYS:
            raise WifiConfigError('Wi-Fi 設定檔第 %d 行的鍵不認得（只接受 %s）' % (number, '、'.join(KEYS)))
        if key in values:
            raise WifiConfigError('Wi-Fi 設定檔第 %d 行重複設定 %s' % (number, key))
        values[key] = value

    ssid = values.get('ssid', '').encode('utf-8')
    if not 1 <= len(ssid) <= SSID_MAX:
        raise WifiConfigError('Wi-Fi SSID 必須是 1–%d bytes（目前 %d bytes）' % (SSID_MAX, len(ssid)))

    password = values.get('password', '')
    if password != '':
        printable = set(string.printable) - set('\t\n\r\x0b\x0c')
        if not all(c in printable for c in password):
            raise WifiConfigError('Wi-Fi 密碼只能用可列印的 ASCII 字元')
        if len(password) == PASSWORD_MAX:
            if not all(c in string.hexdigits for c in password):
                raise WifiConfigError('64 字元的 Wi-Fi 密碼必須是十六進位 PSK')
        elif not PASSWORD_MIN <= len(password) < PASSWORD_MAX:
            raise WifiConfigError('Wi-Fi 密碼長度必須是 %d–%d 字元（目前 %d 字元）'
                                  % (PASSWORD_MIN, PASSWORD_MAX - 1, len(password)))
    return ssid, password.encode('ascii')


def _array(name, value, size):
    padded = value + bytes(size - len(value))
    body = ', '.join('0x%02x' % b for b in padded)
    return 'const uint8_t %s[%d] = {%s};\n' % (name, size, body)


def render(ssid, password):
    return (
        '/* 由 AP_HAL_RTL8735B/hwdef/scripts/rtl8735b_wifi_config.py 產生。\n'
        ' * 可能含 Wi-Fi 帳密：只放在建置目錄，不進版本管理、不交給他人。 */\n'
        '#include <stdint.h>\n\n'
        + _array('rtl8735b_wifi_ssid', ssid, SSID_MAX + 1)
        + 'const uint8_t rtl8735b_wifi_ssid_len = %d;\n' % len(ssid)
        + _array('rtl8735b_wifi_password', password, PASSWORD_MAX + 1)
        + 'const uint8_t rtl8735b_wifi_password_len = %d;\n' % len(password)
    )


def generate(config_path, out_path):
    '''產生 out_path；回傳 'configured' 或 'disabled'。格式錯誤時不寫輸出檔。'''
    if config_path is not None and os.path.isfile(str(config_path)):
        with open(str(config_path), 'rb') as f:
            ssid, password = parse(f.read())
        status = 'configured'
    else:
        ssid, password = b'', b''
        status = 'disabled'
    content = render(ssid, password)
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
    parser = argparse.ArgumentParser('rtl8735b_wifi_config.py')
    parser.add_argument('--config', default=None, help='本機 Wi-Fi STA 設定檔；省略或不存在時停用 Wi-Fi')
    parser.add_argument('out', help='輸出的 C 原始碼路徑')
    args = parser.parse_args()
    try:
        print('Wi-Fi STA: %s' % generate(args.config, args.out))
    except WifiConfigError as e:
        print('Wi-Fi STA 設定錯誤：%s' % e, file=sys.stderr)
        sys.exit(1)
