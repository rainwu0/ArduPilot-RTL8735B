#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
'''
由 hwdef.dat 產生 RTL8735B 板子的 hwdef.h。
目前只處理共用的 define、IMU、COMPASS、BARO 列與 FLASH_LAYOUT；匯流排與腳位的關鍵字之後加入。
FLASH_LAYOUT <名稱> 選 Flash 版面（rtl8735b_flash_layout.py，預設 realtek-ota），以環境變數 RTL8735B_FLASH_LAYOUT
交給 waf 產生 SDK 打包用的分割表；韌體本身不用它，所以不寫進 hwdef.h。
改寫自 libraries/AP_HAL_ESP32/hwdef/scripts/esp32_hwdef.py（ArduPilot 9f648ccabc）。

AP_FLAKE8_CLEAN

'''

import argparse
import os
import sys

sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), '../../../../libraries/AP_HAL/hwdef/scripts'))
sys.path.append(os.path.dirname(os.path.realpath(__file__)))
import hwdef  # noqa:E402
import rtl8735b_flash_layout  # noqa:E402


class RTL8735BHWDef(hwdef.HWDef):

    flash_layout = None

    def write_hwdef_header_content(self, f):
        for d in self.alllines:
            if d.startswith('define '):
                f.write('#define %s\n' % d[7:])

        self.set_flash_layout()
        self.write_IMU_config(f)
        self.write_MAG_config(f)
        self.write_BARO_config(f)

    def set_flash_layout(self):
        name = self.flash_layout or rtl8735b_flash_layout.DEFAULT
        try:
            rtl8735b_flash_layout.validate(name)
        except rtl8735b_flash_layout.LayoutError as e:
            self.error(str(e))
        self.env_vars['RTL8735B_FLASH_LAYOUT'] = name

    def process_line(self, line, depth, a=None):
        '''process one line of pin definition file'''
        # keep all config lines for later use
        self.all_lines.append(line)
        self.alllines.append(line)

        if a is None:
            a = self.split_line(line, posix=False)
        if a and a[0] == 'FLASH_LAYOUT':
            # 後面的疊加檔可以改掉前面的選擇
            if len(a) != 2:
                self.error('FLASH_LAYOUT 要寫成「FLASH_LAYOUT <名稱>」：%s' % line)
            self.flash_layout = a[1]
            return

        super(RTL8735BHWDef, self).process_line(line, depth, a)


if __name__ == '__main__':

    parser = argparse.ArgumentParser("rtl8735b_hwdef.py")
    parser.add_argument(
        '-D', '--outdir', type=str, default="/tmp", help='Output directory')
    parser.add_argument(
        'hwdef', type=str, nargs='+', default=None, help='hardware definition file')
    parser.add_argument(
        '--quiet', action='store_true', default=False, help='quiet running')

    args = parser.parse_args()

    c = RTL8735BHWDef(
        outdir=args.outdir,
        hwdef=args.hwdef,
        quiet=args.quiet,
    )
    c.run()
