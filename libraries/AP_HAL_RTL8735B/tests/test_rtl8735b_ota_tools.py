#!/usr/bin/env python3
'''Tools/scripts/rtl8735b_ota_sign.py、rtl8735b_ota_send.py（OTA 的簽章與傳送工具）的主機測試：
- .ota 檔與金鑰檔的格式、壞檔在連線前擋下、對假接收端的協定與錯誤處理、試開探測：不需要其他套件；
- 簽章：需要 pymonocypher 3.1.3.2，沒有時略過；
- 端對端：簽章工具簽的檔，由傳送端送給真正的接收端（libraries/AP_HAL_RTL8735B/app/ota/ota_session.c 接本機 TCP，
  見 libraries/AP_HAL_RTL8735B/app/ota/tests/ota_session_tcp.c）以 Monocypher 的 C 版驗。需要 pymonocypher、POSIX 的 C 編譯器與
  本庫的 Monocypher，沒有時略過。'''

import base64
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'Tools' / 'scripts'))
import rtl8735b_ota_sign as sign  # noqa: E402
import rtl8735b_ota_send as send  # noqa: E402

# 以獨立的模組名載入測試共用的 ota_cc
_spec = importlib.util.spec_from_file_location('ota_pub_cc', ROOT / 'libraries' / 'AP_HAL_RTL8735B' / 'app' / 'ota' / 'tests' / 'ota_cc.py')
ota_pub_cc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ota_pub_cc)

try:
    import monocypher
    HAVE_MONOCYPHER = monocypher.__version__ == sign.MONOCYPHER_VERSION
except ImportError:
    HAVE_MONOCYPHER = False
NO_MONOCYPHER = '需要 pymonocypher %s' % sign.MONOCYPHER_VERSION

STATUS = struct.Struct('<I')
SECRET = bytes(range(1, 33))
OTHER_SECRET = bytes(range(101, 133))
IMAGE = b'RTL8735B' + bytes((i * 7 + 3) & 0xFF for i in range(50000))


def key_text(kind, key):
    return kind + '_KEYV1:' + base64.b64encode(key).decode()


def fake_ota(image=IMAGE, flags=0):
    '''格式正確、簽章是假的 .ota（不需要 pymonocypher）'''
    header = sign.HEADER.pack(sign.MAGIC, sign.VERSION, flags, len(image), sum(image) & 0xFFFFFFFF)
    return header + hashlib.blake2b(image, digest_size=64).digest() + b'\x5a' * 64 + image


def recv_exact(conn, n):
    data = b''
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            raise EOFError
        data += chunk
    return data


class FakeBoard(threading.Thread):
    '''假接收端：hello、收 144 bytes 或 16 bytes 的試開探測、回狀態、收映像、回最後狀態。silent 時 accept 後直接關閉'''

    def __init__(self, hello=0, request_status=0, final_status=0, silent=False, no_final=False, probe_reply=(0, 2)):
        super().__init__(daemon=True)
        self.hello, self.request_status, self.final_status = hello, request_status, final_status
        self.silent, self.no_final, self.probe_reply = silent, no_final, probe_reply
        self.sock = socket.socket()
        self.sock.bind(('127.0.0.1', 0))
        self.sock.listen(1)
        self.port = self.sock.getsockname()[1]
        self.request = None
        self.image = b''
        self.probed = False

    def run(self):
        conn, _ = self.sock.accept()
        try:
            with conn:
                self.serve(conn)
        except (EOFError, OSError):
            pass
        self.sock.close()

    def serve(self, conn):
        if self.silent:
            return
        conn.sendall(STATUS.pack(self.hello))
        if self.hello != 0:
            return
        first = recv_exact(conn, 16)
        if struct.unpack_from('<I', first)[0] == send.PROBE_MAGIC:
            self.probed = True
            conn.sendall(struct.pack('<II', *self.probe_reply))
            return
        self.request = first + recv_exact(conn, send.REQUEST_LEN - 16)
        conn.sendall(STATUS.pack(self.request_status))
        if self.request_status != 0:
            return
        self.image = recv_exact(conn, sign.HEADER.unpack_from(self.request)[3])
        if not self.no_final:
            conn.sendall(STATUS.pack(self.final_status))


class FormatTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)

    def write(self, data, name='x.ota'):
        p = self.dir / name
        p.write_bytes(data if isinstance(data, bytes) else data.encode())
        return str(p)

    def test_parse_key(self):
        self.assertEqual(sign.parse_key(key_text('PRIVATE', SECRET) + '\n', 'PRIVATE'), SECRET)
        self.assertEqual(sign.parse_key(key_text('PUBLIC', SECRET), 'PUBLIC'), SECRET)

    def test_key_of_the_wrong_kind_is_named(self):
        with self.assertRaisesRegex(sign.SignError, '這是公鑰檔'):
            sign.parse_key(key_text('PUBLIC', SECRET), 'PRIVATE')
        with self.assertRaisesRegex(sign.SignError, '這是私鑰檔'):
            sign.parse_key(key_text('PRIVATE', SECRET), 'PUBLIC')

    def test_bad_keys(self):
        for text in ('hello', 'PRIVATE_KEYV1:***', key_text('PRIVATE', SECRET[:31])):
            with self.assertRaises(sign.SignError):
                sign.parse_key(text, 'PRIVATE')

    def test_image_must_be_firmware(self):
        with self.assertRaisesRegex(sign.SignError, '開頭不是 RTL8735B'):
            sign.load_image(self.write(b'XXXXXXXX' + bytes(100), 'f.bin'))

    def test_signed_message_layout(self):
        msg = sign.signed_message(IMAGE, reboot=False)
        self.assertEqual(len(msg), 80)
        self.assertEqual(sign.HEADER.unpack_from(msg),
                         (0x41544F52, 2, 1, len(IMAGE), sum(IMAGE) & 0xFFFFFFFF))
        self.assertEqual(msg[:4], b'ROTA')
        self.assertEqual(msg[16:], hashlib.blake2b(IMAGE, digest_size=64).digest())

    def test_load_ota(self):
        request, image, flags = send.load_ota(self.write(fake_ota(flags=1)))
        self.assertEqual((len(request), image, flags), (144, IMAGE, 1))

    def test_corrupt_ota_rejected_before_connecting(self):
        good = fake_ota()
        cases = {
            '太短': good[:100],
            'magic': b'X' + good[1:],
            '標頭寫映像': good[:-1],
            '位元組總和': good[:200] + bytes([good[200] ^ 1]) + good[201:],
        }
        for text, data in cases.items():
            with self.subTest(text), self.assertRaisesRegex(send.OtaError, text):
                send.load_ota(self.write(data))
        # 映像改了、位元組總和不變（兩個位元組對調）：雜湊擋下
        img = bytearray(IMAGE)
        img[1000], img[1001] = img[1001], img[1000]
        self.assertNotEqual(bytes(img), IMAGE)
        with self.assertRaisesRegex(send.OtaError, '雜湊不符'):
            send.load_ota(self.write(good[:144] + bytes(img)))
        hash_flipped = good[:20] + bytes([good[20] ^ 1]) + good[21:]
        with self.assertRaisesRegex(send.OtaError, '雜湊不符'):
            send.load_ota(self.write(hash_flipped))


class SenderTest(unittest.TestCase):
    def run_send(self, board, data=None):
        request, image, _ = (data or (fake_ota()[:144], IMAGE, 0))
        board.start()
        try:
            return send.send('127.0.0.1', board.port, request, image, timeout=5)
        finally:
            board.join(5)

    def test_send_ok(self):
        board = FakeBoard()
        self.assertEqual(self.run_send(board), len(IMAGE))
        self.assertEqual(board.request, fake_ota()[:144])
        self.assertEqual(board.image, IMAGE)

    def test_signature_refused(self):
        board = FakeBoard(request_status=12)
        with self.assertRaisesRegex(send.OtaError, 'signature：板子回報 12'):
            self.run_send(board)
        self.assertEqual(board.image, b'')

    def test_bad_image(self):
        with self.assertRaisesRegex(send.OtaError, 'commit：板子回報 11'):
            self.run_send(FakeBoard(final_status=11))

    def test_board_without_key(self):
        with self.assertRaisesRegex(send.OtaError, 'no OTA public key'):
            self.run_send(FakeBoard(hello=13))

    def test_board_closes_without_reply(self):
        with self.assertRaisesRegex(send.OtaError, 'vehicle armed'):
            self.run_send(FakeBoard(silent=True))

    def test_commit_state_unknown(self):
        with self.assertRaisesRegex(send.OtaError, 'installation state unknown'):
            self.run_send(FakeBoard(no_final=True))

    def test_probe(self):
        for reply, text in (((0, 2), 'fw2 已確認'), ((4, 2), 'fw2 還在試開')):
            board = FakeBoard(probe_reply=reply)
            board.start()
            got = send.probe('127.0.0.1', board.port, timeout=5)
            board.join(5)
            self.assertTrue(board.probed)
            self.assertEqual(got, reply)
            self.assertIn(text, send.probe_text(*got))

    def test_main_needs_file_or_probe(self):
        for argv in (['--host', 'x'], ['--host', 'x', '--probe', 'a.ota']):
            with self.assertRaises(SystemExit), contextlib_quiet():
                send.main(argv)

    def test_progress_printer_to_file(self):
        out = io.StringIO()
        t = [0.0]
        show = send.progress_printer(out, now=lambda: t[0], interval=1.0)
        show(10, 100)
        t[0] = 0.5
        show(20, 100)
        t[0] = 1.5
        show(100, 100)
        self.assertEqual(len(out.getvalue().splitlines()), 2)


class contextlib_quiet:
    '''argparse 的錯誤訊息不印到測試輸出'''

    def __enter__(self):
        self.stderr = sys.stderr
        sys.stderr = io.StringIO()

    def __exit__(self, *exc):
        sys.stderr = self.stderr


@unittest.skipUnless(HAVE_MONOCYPHER, NO_MONOCYPHER)
class SignTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.public = monocypher.compute_signing_public_key(SECRET)

    def test_signature_covers_header_and_hash(self):
        data = sign.sign(IMAGE, SECRET)
        request, image, flags = send.load_ota(str(self.write(data)))
        self.assertEqual((image, flags), (IMAGE, 0))
        self.assertTrue(monocypher.signature_check(request[80:], self.public, request[:80]))
        send.check_signature(request, self.public)
        with self.assertRaisesRegex(send.OtaError, '不是 --public-key'):
            send.check_signature(request, monocypher.compute_signing_public_key(OTHER_SECRET))

    def test_no_reboot_flag_is_signed(self):
        data = sign.sign(IMAGE, SECRET, reboot=False)
        self.assertEqual(sign.HEADER.unpack_from(data)[2], sign.FLAG_NO_REBOOT)
        flipped = data[:6] + b'\0\0' + data[8:]
        with self.assertRaises(send.OtaError):
            send.check_signature(flipped[:144], self.public)

    def test_main_writes_file(self):
        fw = self.write(IMAGE, 'firmware_ntz.bin')
        key = self.write(key_text('PRIVATE', SECRET).encode(), 'k_private_key.dat')
        out = self.dir / 'fw.ota'
        with contextlib_quiet():
            self.assertEqual(sign.main(['--private-key', str(key), str(fw), str(out)]), 0)
        self.assertEqual(out.read_bytes(), sign.sign(IMAGE, SECRET))

    def test_main_refuses_public_key(self):
        fw = self.write(IMAGE, 'firmware_ntz.bin')
        key = self.write(key_text('PUBLIC', self.public).encode(), 'k_public_key.dat')
        with contextlib_quiet():
            self.assertEqual(sign.main(['--private-key', str(key), str(fw), str(self.dir / 'fw.ota')]), 1)
        self.assertFalse((self.dir / 'fw.ota').exists())

    def write(self, data, name='x.ota'):
        p = self.dir / name
        p.write_bytes(data)
        return p


@unittest.skipUnless(HAVE_MONOCYPHER and ota_pub_cc.CAN_RUN, NO_MONOCYPHER + ' 與 ' + ota_pub_cc.SKIP_REASON)
class SenderAgainstReceiverTest(unittest.TestCase):
    '''簽章工具簽的檔，經傳送端送給真正的接收端（ota_session.c，Monocypher 的 C 版驗簽章）'''

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.exe = ota_pub_cc.compile_exe(cls.tmp.name, 'ota_session_tcp', ['ota_session.c', 'ota_core.c'],
                                         'ota_session_tcp.c')
        cls.firmware = str(Path(cls.tmp.name) / 'firmware_ntz.bin')
        Path(cls.firmware).write_bytes(IMAGE)
        cls.public = monocypher.compute_signing_public_key(SECRET)
        cls.ota = sign.sign(IMAGE, SECRET)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_receiver(self, board_key, *mode, data=None, probe=False):
        env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        proc = subprocess.Popen([self.exe, board_key.hex() if board_key else '-', self.firmware, *mode],
                                stdout=subprocess.PIPE, text=True, env=env)
        reply = None
        try:
            port = int(proc.stdout.readline().split()[1])
            try:
                if probe:
                    reply = send.probe('127.0.0.1', port, timeout=10)
                else:
                    data = data or self.ota
                    send.send('127.0.0.1', port, data[:144], data[144:], timeout=10)
                error = None
            except send.OtaError as e:
                error = str(e)
            out, _ = proc.communicate(timeout=60)
        finally:
            if proc.poll() is None:
                proc.kill()
        self.assertEqual(proc.returncode, 0)
        fields = out.split()
        return error, dict(zip(fields[0::2], map(int, fields[1::2]))), reply

    def refused(self, result, code):
        '''拒收：目前的槽與分割表沒變、不重開、另一槽沒裝上'''
        self.assertEqual(result['result'], code)
        self.assertEqual((result['reboot'], result['match'], result['running_same']), (0, 0, 1))

    def test_installs(self):
        error, result, _ = self.run_receiver(self.public)
        self.assertIsNone(error)
        self.assertEqual((result['result'], result['reboot'], result['match'], result['running_same']), (0, 1, 1, 1))

    def test_signed_with_another_key(self):
        error, result, _ = self.run_receiver(self.public, data=sign.sign(IMAGE, OTHER_SECRET))
        self.assertIn('signature check failed', error)
        self.refused(result, 12)
        self.assertEqual((result['erase'], result['program']), (0, 0))

    def test_header_changed_after_signing(self):
        data = bytearray(self.ota)
        data[12] ^= 1
        error, result, _ = self.run_receiver(self.public, data=bytes(data))
        self.refused(result, 12)
        self.assertEqual((result['erase'], result['program']), (0, 0))

    def test_image_tampered_in_transit(self):
        data = bytearray(self.ota)
        data[144 + 30000] ^= 1
        error, result, _ = self.run_receiver(self.public, data=bytes(data))
        self.assertIn('commit：板子回報 11', error)
        self.refused(result, 11)

    def test_board_without_key(self):
        error, result, _ = self.run_receiver(None)
        self.assertIn('no OTA public key', error)
        self.refused(result, 13)

    def test_armed(self):
        error, result, _ = self.run_receiver(self.public, 'armed')
        self.assertIn('vehicle armed', error)
        self.refused(result, 2)

    def test_probe(self):
        for mode, code in (((), 0), (('trial',), 4)):
            with self.subTest(mode):
                error, result, reply = self.run_receiver(self.public, *mode, probe=True)
                self.assertIsNone(error)
                self.assertEqual(reply, (code, 1))
                self.refused(result, code)
                self.assertEqual((result['erase'], result['program']), (0, 0))


if __name__ == '__main__':
    unittest.main()
