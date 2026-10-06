#!/usr/bin/env python3
'''
由 hwdef.dat 產生 RTL8735B 板子的 hwdef.h。
目前只處理共用的 define、IMU、COMPASS、BARO 列；匯流排與腳位的關鍵字之後加入。

AP_FLAKE8_CLEAN

'''

import argparse
import os
import sys

sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), '../../../../libraries/AP_HAL/hwdef/scripts'))
import hwdef  # noqa:E402


class RTL8735BHWDef(hwdef.HWDef):

    def write_hwdef_header_content(self, f):
        for d in self.alllines:
            if d.startswith('define '):
                f.write('#define %s\n' % d[7:])

        self.write_IMU_config(f)
        self.write_MAG_config(f)
        self.write_BARO_config(f)

    def process_line(self, line, depth, a=None):
        '''process one line of pin definition file'''
        # keep all config lines for later use
        self.all_lines.append(line)
        self.alllines.append(line)

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
