#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
'''
由 hwdef.dat 產生 RTL8735B 開發板的 hwdef.h。
處理共用的 define、IMU、COMPASS、BARO 列、FLASH_LAYOUT，以及描述開發板周邊的關鍵字：

  RTL8735B_I2CBUS <邏輯匯流排> <SDK 編號 0-3> <SDA> <SCL> <時脈 Hz> <internal|external>
  RTL8735B_SPIBUS <邏輯匯流排> <SDK 編號 0-1> <MOSI> <MISO> <SCLK> <預設時脈 Hz>
  RTL8735B_SPIDEV <名稱> <邏輯匯流排> <CS> <mode 0-3> <低速 Hz> <高速 Hz>
  RTL8735B_RCOUT  <腳位>                      出現的順序就是輸出通道 1、2、…
  RTL8735B_GPIO   <邏輯編號> <腳位> <in|out>
  RTL8735B_ADC    <邏輯編號> <腳位 PIN_F0-PIN_F3、PIN_A0-PIN_A3>    每一行一支類比腳

每種關鍵字轉成 hwdef.h 的一個表格巨集（HAL_RTL8735B_*）與數量；疊加檔以「undef <關鍵字>」清空整組再重寫。
腳位能不能當該周邊用，由 SDK 初始化時判斷。
FLASH_LAYOUT <名稱> 選 Flash 版面（rtl8735b_flash_layout.py，預設 realtek-ota），以環境變數 RTL8735B_FLASH_LAYOUT
交給 waf 產生 SDK 打包用的分割表；韌體本身不用它，所以不寫進 hwdef.h。
改寫自 libraries/AP_HAL_ESP32/hwdef/scripts/esp32_hwdef.py（ArduPilot 9f648ccabc）。

AP_FLAKE8_CLEAN

'''

import argparse
import os
import re
import sys

sys.path.append(os.path.join(os.path.dirname(os.path.realpath(__file__)), '../../../../libraries/AP_HAL/hwdef/scripts'))
sys.path.append(os.path.dirname(os.path.realpath(__file__)))
import hwdef  # noqa:E402
import rtl8735b_flash_layout  # noqa:E402

# 關鍵字 -> 欄位數
KEYWORDS = {
    'RTL8735B_I2CBUS': 6,
    'RTL8735B_SPIBUS': 6,
    'RTL8735B_SPIDEV': 6,
    'RTL8735B_RCOUT': 1,
    'RTL8735B_GPIO': 3,
    'RTL8735B_ADC': 2,
}

# 表的大小與 HAL 的寫法相同：I²C 匯流排最多 4 條（SDK 的 I2C0–I2C3，bus_shim.c 每條一組狀態）；
# SPI 匯流排一條；SPI 裝置最多 4 個（bus_shim.c 的軟體 CS 表）；PWM 最多 12 路（PWM0–11）；類比輸入最多 8 支（ADC 通道 0–7）。
LIMITS = {
    'RTL8735B_I2CBUS': (1, 4),
    'RTL8735B_SPIBUS': (1, 1),
    'RTL8735B_SPIDEV': (1, 4),
    'RTL8735B_RCOUT': (1, 12),
    'RTL8735B_GPIO': (1, 16),
    'RTL8735B_ADC': (1, 8),
}

# 類比輸入：PF0–PF3 是 ADC 通道 0–3，PA0–PA3 是外接通道 4–7（SDK component/mbed/targets/hal/rtl8735b/analogin_api.c
# 的腳位表；component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_adc.h 的 HP_ADC_CH_NO、HP_ADC_EXCH_NO）
ADC_CHANNELS = {'PIN_F0': 0, 'PIN_F1': 1, 'PIN_F2': 2, 'PIN_F3': 3,
                'PIN_A0': 4, 'PIN_A1': 5, 'PIN_A2': 6, 'PIN_A3': 7}


class RTL8735BHWDef(hwdef.HWDef):

    flash_layout = None

    def __init__(self, **kwargs):
        super(RTL8735BHWDef, self).__init__(**kwargs)
        self.board = {k: [] for k in KEYWORDS}

    def write_hwdef_header_content(self, f):
        for d in self.alllines:
            if d.startswith('define '):
                f.write('#define %s\n' % d[7:])

        self.write_board_tables(f)
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
        if a and a[0] in KEYWORDS:
            if len(a) - 1 != KEYWORDS[a[0]]:
                self.error('%s 要有 %u 個欄位：%s' % (a[0], KEYWORDS[a[0]], line))
            self.board[a[0]].append(a[1:])
            return

        super(RTL8735BHWDef, self).process_line(line, depth, a)

    def process_line_undef(self, line, depth, a):
        # 基底類別只清 define 列與它認得的 IMU、COMPASS、BARO、ROMFS；本移植的關鍵字要自己清，
        # 否則疊加檔的 undef 不會生效，新舊兩組並存。
        super(RTL8735BHWDef, self).process_line_undef(line, depth, a)
        for u in a[1:]:
            if u in KEYWORDS:
                self.board[u] = []

    def pin(self, keyword, value):
        if not re.match(r'^PIN_[A-Z]\d{1,2}$', value):
            self.error('%s 的腳位要寫成 PIN_<埠><編號>（例如 PIN_F2）：%s' % (keyword, value))
        return value

    def number(self, keyword, value, low=0, high=None):
        if not re.match(r'^\d+$', value) or int(value) < low or (high is not None and int(value) > high):
            limit = '%u–%u' % (low, high) if high is not None else '>= %u' % low
            self.error('%s 的欄位要是 %s 的整數：%s' % (keyword, limit, value))
        return int(value)

    def choice(self, keyword, value, choices):
        if value not in choices:
            self.error('%s 的欄位要是 %s 之一：%s' % (keyword, '、'.join(choices), value))
        return value

    def write_board_tables(self, f):
        for keyword, (low, high) in LIMITS.items():
            n = len(self.board[keyword])
            if not low <= n <= high:
                self.error('%s 要有 %u–%u 列，現在 %u 列' % (keyword, low, high, n))

        # 每支腳被哪個設定用到，最後檢查同一支腳有沒有被用兩次
        uses = []

        k = 'RTL8735B_I2CBUS'
        rows = []
        i2c_buses = set()
        i2c_indexes = set()
        for (bus, index, sda, scl, clock, kind) in self.board[k]:
            # 每條匯流排在 ArduPilot 有自己的編號，在 SDK 有自己的控制器，兩者都不能重複
            if self.number(k, bus, 0, 31) in i2c_buses:
                self.error('%s 的邏輯匯流排 %s 重複' % (k, bus))
            i2c_buses.add(int(bus))
            if self.number(k, index, 0, 3) in i2c_indexes:
                self.error('%s 的 SDK 控制器 I2C%s 重複' % (k, index))
            i2c_indexes.add(int(index))
            uses += [(sda, '%s %s SDA' % (k, bus)), (scl, '%s %s SCL' % (k, bus))]
            rows.append('{%u, %u, %s, %s, %uU, %u}' % (
                self.number(k, bus, 0, 31), self.number(k, index, 0, 3), self.pin(k, sda), self.pin(k, scl),
                self.number(k, clock, 1), self.choice(k, kind, ('internal', 'external')) == 'internal'))
        self.write_table(f, 'I2C buses', 'HAL_RTL8735B_I2C_BUSES', 'HAL_RTL8735B_I2C_BUS_COUNT', rows)

        k = 'RTL8735B_SPIBUS'
        rows = []
        spi_buses = set()
        for (bus, index, mosi, miso, sclk, clock) in self.board[k]:
            spi_buses.add(self.number(k, bus, 0, 31))
            uses += [(mosi, '%s %s MOSI' % (k, bus)), (miso, '%s %s MISO' % (k, bus)),
                     (sclk, '%s %s SCLK' % (k, bus))]
            rows.append('{%u, %u, %s, %s, %s, %uU}' % (
                self.number(k, bus, 0, 31), self.number(k, index, 0, 1), self.pin(k, mosi), self.pin(k, miso),
                self.pin(k, sclk), self.number(k, clock, 1)))
        self.write_table(f, 'SPI buses', 'HAL_RTL8735B_SPI_BUSES', 'HAL_RTL8735B_SPI_BUS_COUNT', rows)

        k = 'RTL8735B_SPIDEV'
        rows = []
        names = set()
        for device_id, (name, bus, cs, mode, low_hz, high_hz) in enumerate(self.board[k]):
            if not re.match(r'^\w+$', name):
                self.error('%s 的名稱只能用英數字與底線：%s' % (k, name))
            if name in names:
                self.error('%s 的名稱 %s 重複' % (k, name))
            names.add(name)
            uses.append((cs, '%s %s CS' % (k, name)))
            if self.number(k, bus) not in spi_buses:
                self.error('%s %s 的匯流排 %s 沒有以 RTL8735B_SPIBUS 宣告' % (k, name, bus))
            # 欄位順序同 BusConfig.h 的 RTL8735BSPIDeviceConfig；device_id 是表中的順序，
            # 讓同一條匯流排上的裝置（例如兩顆同型感測器）在 ArduPilot 的裝置 ID 分得開
            rows.append('{"%s", %u, %s, %uU, %uU, HAL_RTL8735B_SPI_TIMEOUT_MS, %u, %u}' % (
                name, self.number(k, bus), self.pin(k, cs), self.number(k, low_hz, 1),
                self.number(k, high_hz, 1), device_id, self.number(k, mode, 0, 3)))
        self.write_table(f, 'SPI devices', 'HAL_RTL8735B_SPI_DEVICES', 'HAL_RTL8735B_SPI_DEVICE_COUNT', rows)

        k = 'RTL8735B_RCOUT'
        rows = [self.pin(k, p) for (p,) in self.board[k]]
        uses += [(p, '%s %u' % (k, i + 1)) for i, (p,) in enumerate(self.board[k])]
        self.write_table(f, 'RC outputs', 'HAL_RTL8735B_RCOUT_PINS', 'HAL_RTL8735B_RCOUT_COUNT', rows)

        k = 'RTL8735B_GPIO'
        rows = []
        seen = set()
        for (logical, pin, direction) in self.board[k]:
            n = self.number(k, logical, 0, 255)
            uses.append((pin, '%s %s' % (k, logical)))
            if n in seen:
                self.error('%s 的邏輯編號 %u 重複' % (k, n))
            seen.add(n)
            rows.append('{%u, %s, %u}' % (n, self.pin(k, pin), self.choice(k, direction, ('in', 'out')) == 'out'))
        self.write_table(f, 'GPIO pins', 'HAL_RTL8735B_GPIO_PINS', 'HAL_RTL8735B_GPIO_COUNT', rows)

        k = 'RTL8735B_ADC'
        rows = []
        seen = set()
        for (logical, pin) in self.board[k]:
            if self.pin(k, pin) not in ADC_CHANNELS:
                self.error('%s 只支援 PIN_F0–PIN_F3、PIN_A0–PIN_A3：%s' % (k, pin))
            # 254、255 是 ArduPilot 的 ANALOG_INPUT_BOARD_VCC、ANALOG_INPUT_NONE
            n = self.number(k, logical, 0, 253)
            if n in seen:
                self.error('%s 的邏輯編號 %u 重複' % (k, n))
            seen.add(n)
            rows.append('{%u, %s, %u}' % (n, pin, ADC_CHANNELS[pin]))
            uses.append((pin, '%s %s' % (k, logical)))
        self.write_table(f, 'ADC pins', 'HAL_RTL8735B_ADC_PINS', 'HAL_RTL8735B_ADC_COUNT', rows)

        # 啟用中的序列埠腳位（停用的序列埠不占腳位）
        for line in self.alllines:
            m = re.match(r'define\s+(HAL_RTL8735B_SERIAL(\d)_(?:TX|RX)_PIN)\s+(PIN_\w+)', line)
            if m and self.intdefines.get('HAL_RTL8735B_SERIAL%s_ENABLED' % m.group(2), 0) == 1:
                uses.append((m.group(3), m.group(1)))
        owner = {}
        for pin, source in uses:
            if pin in owner:
                self.error('%s 同時用在 %s 與 %s' % (pin, owner[pin], source))
            owner[pin] = source

    def write_table(self, f, description, define_name, count_name, rows):
        self.write_device_table(f, description, define_name, rows)
        f.write('#define %s %u\n' % (count_name, len(rows)))


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
