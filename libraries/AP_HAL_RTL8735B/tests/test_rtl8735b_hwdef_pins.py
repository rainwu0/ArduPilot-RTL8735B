#!/usr/bin/env python3
'''hwdef 產生器（hwdef/scripts/rtl8735b_hwdef.py）的板子描述關鍵字：
RTL8735B_I2CBUS、RTL8735B_SPIBUS、RTL8735B_SPIDEV、RTL8735B_RCOUT、RTL8735B_GPIO、RTL8735B_ADC。

產生器把它們轉成 hwdef.h 的表格巨集（HAL_RTL8735B_*）與數量；疊加檔以 undef 清空整組再重寫。
'''

import gc
import io
import os
from contextlib import redirect_stdout
from pathlib import Path
import re
import sys
import tempfile
import unittest
import warnings

ROOT = Path(__file__).resolve().parents[3]   # 儲存庫根目錄（tests → AP_HAL_RTL8735B → libraries → 根）
HAL = ROOT / 'libraries/AP_HAL_RTL8735B'
sys.path.insert(0, str(HAL / 'hwdef/scripts'))
import rtl8735b_hwdef  # noqa: E402

EMPTY = HAL / 'hwdef/rtl8735b-empty/hwdef.dat'

# 上游 hwdef.py 開檔後不關；產生器報錯時檔案物件留在例外的 traceback 裡，晚一點才回收，
# 會在 catch_warnings 之外發 ResourceWarning。不是本庫的程式，整個檔案忽略。
warnings.filterwarnings('ignore', category=ResourceWarning)


class HwdefError(Exception):
    '''產生器以 sys.exit 結束；轉成一般例外並帶上它印出的訊息，失敗時看得到原因'''


def generate(*overlays, base=EMPTY):
    '''以 base 依序加上各份疊加檔（每份是一串行）產生 hwdef.h，回傳內容'''
    with tempfile.TemporaryDirectory() as tmp:
        files = [str(base)]
        for i, lines in enumerate(overlays):
            overlay = Path(tmp) / ('overlay%d.dat' % i)
            overlay.write_text('\n'.join(lines) + '\n', encoding='utf-8')
            files.append(str(overlay))
        h = rtl8735b_hwdef.RTL8735BHWDef(outdir=tmp, hwdef=files, quiet=True)
        out = io.StringIO()
        # 上游 hwdef.py 開檔後不關，回收時發 ResourceWarning；不是本庫的程式
        with redirect_stdout(out), warnings.catch_warnings():
            warnings.simplefilter('ignore', ResourceWarning)
            try:
                h.run()
            except SystemExit:
                raise HwdefError(out.getvalue().strip()) from None
            finally:
                gc.collect()
        return (Path(tmp) / 'hwdef.h').read_text(encoding='utf-8')


def table(header, name):
    '''回傳表格巨集的各列（去掉空白），找不到時回傳 None'''
    m = re.search(r'^#define %s \\\n((?:.*\\\n)*.*)$' % re.escape(name), header, re.M)
    if m is None:
        return None
    return [re.sub(r'\s+', '', row).rstrip(',\\') for row in m.group(1).split('\n')]


def count(header, name):
    m = re.search(r'^#define %s (\d+)$' % re.escape(name), header, re.M)
    return int(m.group(1)) if m else None


class UndefTest(unittest.TestCase):
    def test_undef_gpio_then_readd_keeps_only_new_rows(self):
        # 使用者會碰到的安靜失敗：undef 若沒有清空清單，舊的 GPIO 會和新的並存。
        # 第一份疊加檔先放一組，第二份 undef 後重寫，結果只能有第二組。
        header = generate(['RTL8735B_GPIO 7 PIN_D17 in',
                           'RTL8735B_GPIO 25 PIN_E6 out'],
                          ['undef RTL8735B_GPIO',
                           'RTL8735B_GPIO 17 PIN_D17 in',
                           'RTL8735B_GPIO 23 PIN_F9 out'])
        self.assertEqual(table(header, 'HAL_RTL8735B_GPIO_PINS'),
                         ['{17,PIN_D17,0}', '{23,PIN_F9,1}'])
        self.assertEqual(count(header, 'HAL_RTL8735B_GPIO_COUNT'), 2)

    def test_undef_each_keyword_replaces_the_whole_group(self):
        cases = [
            ('RTL8735B_I2CBUS', 'RTL8735B_I2CBUS 1 2 PIN_E4 PIN_E3 100000 internal',
             'HAL_RTL8735B_I2C_BUSES', '{1,2,PIN_E4,PIN_E3,100000U,1}'),
            # SPI0（PE1–PE3）會和 SERIAL4 的 UART3 衝突，這裡改時脈測 undef
            ('RTL8735B_SPIBUS', 'RTL8735B_SPIBUS 1 1 PIN_F7 PIN_F5 PIN_F6 2000000',
             'HAL_RTL8735B_SPI_BUSES', '{1,1,PIN_F7,PIN_F5,PIN_F6,2000000U}'),
            ('RTL8735B_RCOUT', 'RTL8735B_RCOUT PIN_F10', 'HAL_RTL8735B_RCOUT_PINS', 'PIN_F10'),
            ('RTL8735B_ADC', 'RTL8735B_ADC 4 PIN_F3', 'HAL_RTL8735B_ADC_PINS', '{4,PIN_F3,3}'),
        ]
        for keyword, row, macro, expected in cases:
            with self.subTest(keyword=keyword):
                header = generate(['undef ' + keyword, row])
                self.assertEqual(table(header, macro), [expected])
        header = generate(['undef RTL8735B_SPIDEV', 'RTL8735B_SPIDEV icm42688 1 PIN_F8 3 2000000 8000000'])
        self.assertEqual(table(header, 'HAL_RTL8735B_SPI_DEVICES'),
                         ['{"icm42688",1,PIN_F8,2000000U,8000000U,HAL_RTL8735B_SPI_TIMEOUT_MS,0,3}'])


class DefaultBoardTest(unittest.TestCase):
    '''rtl8735b-empty 以新關鍵字描述，值與 HUB 8735 ultra 的接法相同'''

    @classmethod
    def setUpClass(cls):
        cls.header = generate()

    def test_buses(self):
        self.assertEqual(table(self.header, 'HAL_RTL8735B_I2C_BUSES'), ['{1,1,PIN_F2,PIN_F1,400000U,0}'])
        self.assertEqual(count(self.header, 'HAL_RTL8735B_I2C_BUS_COUNT'), 1)
        self.assertEqual(table(self.header, 'HAL_RTL8735B_SPI_BUSES'), ['{1,1,PIN_F7,PIN_F5,PIN_F6,1000000U}'])
        self.assertEqual(count(self.header, 'HAL_RTL8735B_SPI_BUS_COUNT'), 1)
        self.assertEqual(table(self.header, 'HAL_RTL8735B_SPI_DEVICES'),
                         ['{"bmi160",1,PIN_F8,1000000U,10000000U,HAL_RTL8735B_SPI_TIMEOUT_MS,0,0}'])
        self.assertEqual(count(self.header, 'HAL_RTL8735B_SPI_DEVICE_COUNT'), 1)

    def test_outputs_and_pins(self):
        self.assertEqual(table(self.header, 'HAL_RTL8735B_RCOUT_PINS'),
                         ['PIN_F14', 'PIN_F11', 'PIN_F12', 'PIN_F13'])
        self.assertEqual(count(self.header, 'HAL_RTL8735B_RCOUT_COUNT'), 4)
        self.assertEqual(table(self.header, 'HAL_RTL8735B_GPIO_PINS'),
                         ['{7,PIN_D17,0}', '{25,PIN_E6,1}', '{26,PIN_F9,1}'])
        self.assertEqual(count(self.header, 'HAL_RTL8735B_GPIO_COUNT'), 3)
        self.assertEqual(table(self.header, 'HAL_RTL8735B_ADC_PINS'), ['{2,PIN_F0,0}'])
        self.assertEqual(count(self.header, 'HAL_RTL8735B_ADC_COUNT'), 1)

    def test_old_macros_are_gone(self):
        # 不留舊名稱的相容別名：每個值只有一個來源
        for old in (r'HAL_RTL8735B_I2C1_\w+', r'HAL_RTL8735B_SPI1_\w+', 'HAL_RTL8735B_BMI160_CS_PIN',
                    r'HAL_RTL8735B_RCOUT_PIN\d+', 'HAL_RTL8735B_RCOUT_CHANNELS', r'HAL_RTL8735B_GPIO_PIN\d+',
                    'HAL_RTL8735B_ADC_PIN0', 'HAL_RTL8735B_ADC_CHANNELS', 'HAL_RTL8735B_ADC_LOGICAL0'):
            with self.subTest(old=old):
                self.assertIsNone(re.search(r'^#define %s\b' % old, self.header, re.M))


class Amb82MiniTest(unittest.TestCase):
    '''amb82-mini：include rtl8735b-empty，只把 GPIO 編號改成 AMB82-mini 的 D 編號，其餘周邊腳位相同'''

    @classmethod
    def setUpClass(cls):
        cls.header = generate(base=HAL / 'hwdef/amb82-mini/hwdef.dat')
        cls.default = generate()

    def test_board_name(self):
        self.assertIn('#define HAL_RTL8735B_BOARD_NAME "amb82-mini"', self.header)

    def test_gpio_uses_amb82_mini_numbers(self):
        self.assertEqual(table(self.header, 'HAL_RTL8735B_GPIO_PINS'),
                         ['{17,PIN_D17,0}', '{24,PIN_E6,1}', '{23,PIN_F9,1}'])

    def test_other_tables_match_rtl8735b_empty(self):
        for name in ('HAL_RTL8735B_I2C_BUSES', 'HAL_RTL8735B_SPI_BUSES', 'HAL_RTL8735B_SPI_DEVICES',
                     'HAL_RTL8735B_RCOUT_PINS', 'HAL_RTL8735B_ADC_PINS'):
            with self.subTest(name=name):
                self.assertEqual(table(self.header, name), table(self.default, name))


def without_board_name(header):
    return re.sub(r'^#define HAL_RTL8735B_BOARD_NAME .*\n', '', header, flags=re.M)


class Hub8735UltraTest(unittest.TestCase):
    '''hub8735ultra：include rtl8735b-empty，只改板名；rtl8735b-empty 保持原樣，給既有的建置'''

    @classmethod
    def setUpClass(cls):
        cls.header = generate(base=HAL / 'hwdef/hub8735ultra/hwdef.dat')
        cls.default = generate()

    def test_board_name(self):
        self.assertIn('#define HAL_RTL8735B_BOARD_NAME "hub8735ultra"', self.header)

    def test_everything_else_matches_rtl8735b_empty(self):
        # 匯流排、PWM、GPIO、類比輸入、序列埠與其他 define 全部相同
        self.assertEqual(without_board_name(self.header), without_board_name(self.default))

    def test_rtl8735b_empty_keeps_its_name(self):
        # 防止反過來讓 rtl8735b-empty include hub8735ultra：之後加進 hub8735ultra 的腳位會帶進既有的 rtl8735b-empty 建置
        self.assertIn('#define HAL_RTL8735B_BOARD_NAME "rtl8735b-empty"', self.default)
        self.assertNotIn('hub8735ultra', EMPTY.read_text(encoding='utf-8'))


class RulesTest(unittest.TestCase):
    '''寫錯要在建置時報錯，不留到開機'''

    def assertRejected(self, *overlays):
        with self.assertRaises(HwdefError):
            generate(*overlays)

    def test_adc_pin_maps_to_channel(self):
        for pin, channel in (('PIN_F0', 0), ('PIN_F1', 1), ('PIN_F2', 2), ('PIN_F3', 3)):
            with self.subTest(pin=pin):
                # PF1、PF2 是預設 I²C1 的腳：先把 I²C 換到 I2C2（PE4、PE3）
                header = generate(['undef RTL8735B_I2CBUS', 'RTL8735B_I2CBUS 1 2 PIN_E4 PIN_E3 400000 external',
                                   'undef RTL8735B_ADC', 'RTL8735B_ADC 2 ' + pin])
                self.assertEqual(table(header, 'HAL_RTL8735B_ADC_PINS'), ['{2,%s,%u}' % (pin, channel)])

    def test_adc_only_on_pf0_to_pf3(self):
        self.assertRejected(['undef RTL8735B_ADC', 'RTL8735B_ADC 2 PIN_A0'])

    def test_field_counts(self):
        self.assertRejected(['RTL8735B_GPIO 7 PIN_D17'])
        self.assertRejected(['undef RTL8735B_I2CBUS', 'RTL8735B_I2CBUS 1 1 PIN_F2 PIN_F1 400000'])
        self.assertRejected(['undef RTL8735B_SPIBUS', 'RTL8735B_SPIBUS 1 1 PIN_F7 PIN_F5 PIN_F6'])
        self.assertRejected(['undef RTL8735B_SPIDEV', 'RTL8735B_SPIDEV bmi160 1 PIN_F8 0 1000000'])
        self.assertRejected(['undef RTL8735B_RCOUT', 'RTL8735B_RCOUT PIN_F14 PIN_F11'])
        self.assertRejected(['undef RTL8735B_ADC', 'RTL8735B_ADC PIN_F0'])

    def test_field_values(self):
        self.assertRejected(['undef RTL8735B_GPIO', 'RTL8735B_GPIO 7 PIN_D17 output'])
        self.assertRejected(['undef RTL8735B_GPIO', 'RTL8735B_GPIO x PIN_D17 in'])
        self.assertRejected(['undef RTL8735B_GPIO', 'RTL8735B_GPIO 7 PD17 in'])
        self.assertRejected(['undef RTL8735B_I2CBUS', 'RTL8735B_I2CBUS 1 4 PIN_F2 PIN_F1 400000 external'])
        # 邏輯匯流排要放得進 32 位元的匯流排遮罩
        self.assertRejected(['undef RTL8735B_I2CBUS', 'RTL8735B_I2CBUS 32 1 PIN_F2 PIN_F1 400000 external'])
        self.assertRejected(['undef RTL8735B_I2CBUS', 'RTL8735B_I2CBUS 1 1 PIN_F2 PIN_F1 fast external'])
        self.assertRejected(['undef RTL8735B_I2CBUS', 'RTL8735B_I2CBUS 1 1 PIN_F2 PIN_F1 400000 both'])
        self.assertRejected(['undef RTL8735B_SPIBUS', 'RTL8735B_SPIBUS 1 2 PIN_F7 PIN_F5 PIN_F6 1000000'])
        self.assertRejected(['undef RTL8735B_SPIDEV', 'RTL8735B_SPIDEV bmi160 1 PIN_F8 4 1000000 10000000'])

    def test_gpio_logical_numbers_are_unique(self):
        self.assertRejected(['RTL8735B_GPIO 7 PIN_E1 in'])

    def test_spi_devices_get_distinct_device_ids(self):
        # 每個 SPI 裝置有自己的 device_id（表中的順序），同型的兩顆感測器在 ArduPilot 的裝置 ID 才分得開
        header = generate(['undef RTL8735B_SPIDEV',
                           'RTL8735B_SPIDEV bmi160 1 PIN_F8 0 1000000 10000000',
                           'RTL8735B_SPIDEV icm42688 1 PIN_E4 3 2000000 8000000'])
        self.assertEqual(table(header, 'HAL_RTL8735B_SPI_DEVICES'),
                         ['{"bmi160",1,PIN_F8,1000000U,10000000U,HAL_RTL8735B_SPI_TIMEOUT_MS,0,0}',
                          '{"icm42688",1,PIN_E4,2000000U,8000000U,HAL_RTL8735B_SPI_TIMEOUT_MS,1,3}'])

    def test_spi_device_needs_its_bus(self):
        self.assertRejected(['undef RTL8735B_SPIDEV', 'RTL8735B_SPIDEV bmi160 2 PIN_F8 0 1000000 10000000'])

    def assertRejectedNaming(self, overlay, *names):
        '''報錯，而且訊息指出衝突的兩個來源'''
        with self.assertRaises(HwdefError) as cm:
            generate(overlay)
        for name in names:
            self.assertIn(name, str(cm.exception))

    def test_pin_used_twice_is_rejected(self):
        # 同一支腳不能被兩個設定用到；報錯要指出衝突的兩個來源
        self.assertRejectedNaming(['RTL8735B_GPIO 3 PIN_F14 out'], 'PIN_F14', 'RTL8735B_GPIO', 'RTL8735B_RCOUT')
        self.assertRejectedNaming(['RTL8735B_RCOUT PIN_F11'], 'PIN_F11', 'RTL8735B_RCOUT')
        self.assertRejectedNaming(['RTL8735B_SPIDEV imu2 1 PIN_F8 3 1000000 8000000'], 'PIN_F8', 'RTL8735B_SPIDEV')
        self.assertRejectedNaming(['undef RTL8735B_ADC', 'RTL8735B_ADC 2 PIN_F1'], 'PIN_F1', 'RTL8735B_I2CBUS', 'RTL8735B_ADC')
        self.assertRejectedNaming(['RTL8735B_GPIO 4 PIN_F6 in'], 'PIN_F6', 'RTL8735B_SPIBUS')
        # 啟用中的序列埠腳位也算；停用的不算（SERIAL0 的 LOG UART 腳位停用，可以另作他用）
        self.assertRejectedNaming(['RTL8735B_GPIO 8 PIN_D15 out'], 'PIN_D15', 'HAL_RTL8735B_SERIAL1_TX_PIN')
        header = generate(['RTL8735B_GPIO 17 PIN_F4 out'])
        self.assertIn('{17,PIN_F4,1}', table(header, 'HAL_RTL8735B_GPIO_PINS'))

    def test_spi_device_names_are_unique(self):
        self.assertRejected(['RTL8735B_SPIDEV bmi160 1 PIN_E4 0 1000000 8000000'])

    def test_logical_number_ranges(self):
        # 邏輯編號在 C 端是 uint8_t：超出範圍不能留到編譯或執行時
        self.assertRejected(['RTL8735B_GPIO 256 PIN_E4 out'])
        self.assertRejected(['undef RTL8735B_SPIBUS', 'undef RTL8735B_SPIDEV',
                             'RTL8735B_SPIBUS 32 1 PIN_F7 PIN_F5 PIN_F6 1000000',
                             'RTL8735B_SPIDEV bmi160 32 PIN_F8 0 1000000 10000000'])
        # ADC 的 254、255 是 ArduPilot 的 ANALOG_INPUT_BOARD_VCC、ANALOG_INPUT_NONE
        self.assertRejected(['undef RTL8735B_ADC', 'RTL8735B_ADC 254 PIN_F0'])
        header = generate(['RTL8735B_GPIO 255 PIN_E4 out'])
        self.assertIn('{255,PIN_E4,1}', table(header, 'HAL_RTL8735B_GPIO_PINS'))

    def test_group_sizes(self):
        # 這一步只搬資料：I²C、SPI 匯流排與類比輸入各一個，SPI 裝置 1–4 個（CS 表 4 格），PWM 1–12 路
        self.assertRejected(['RTL8735B_I2CBUS 2 2 PIN_E4 PIN_E3 400000 external'])
        self.assertRejected(['undef RTL8735B_I2CBUS'])
        self.assertRejected(['RTL8735B_SPIBUS 2 0 PIN_E3 PIN_E2 PIN_E1 1000000'])
        self.assertRejected(['RTL8735B_ADC 3 PIN_F3'])
        self.assertRejected(['undef RTL8735B_SPIDEV'])
        self.assertRejected(['RTL8735B_SPIDEV d%u 1 PIN_E%u 0 1000000 1000000' % (i, i) for i in range(1, 5)])
        self.assertRejected(['undef RTL8735B_RCOUT'])
        self.assertRejected(['RTL8735B_RCOUT PIN_S%u' % i for i in range(9)])
        self.assertRejected(['undef RTL8735B_GPIO'])
        header = generate(['RTL8735B_RCOUT PIN_S%u' % i for i in range(8)])
        self.assertEqual(count(header, 'HAL_RTL8735B_RCOUT_COUNT'), 12)


if __name__ == '__main__':
    unittest.main()
