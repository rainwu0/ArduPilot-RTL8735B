# Flash the firmware

[繁體中文](flashing.zh-TW.md)

How to write firmware to an RTL8735B board: `flash_ntz.bin` through its USB serial port, by hand on Windows or with `--upload` at the end of a build, or a signed image over Wi-Fi to a board running the board application `app/ota`. Building the image is in [Build the firmware](building.md).

## Download mode

The level of PA5 at reset decides whether the chip enters download mode. Each board has its own buttons for this; for the HUB 8735 ultra, see [Loading Firmware](../hwdef/hub8735ultra/README.md#loading-firmware) in its README.

Flash soon after entering download mode. After a while the tool reports `ping fail` or `ucfg fail`; entering download mode again clears it.

## Flash by hand on Windows

The tool runs on Windows, because WSL2 does not see USB serial ports by default.

1. Unpack the SDK's `tools/Pro2_PG_tool _v1.3.0.zip` (the file name contains a space). It contains `uartfwburn.exe`.
2. Connect the board's USB serial port to the computer and look up its COM port in Device Manager.
3. Close programs that hold that COM port open, such as ground station software and serial terminals, or switch them to a network connection.
4. Put the chip into download mode.
5. Run the tool, with `-p` set to the port and an absolute path after `-f`:

   ```powershell
   .\uartfwburn.exe -p COM7 -f <path>\flash_ntz.bin -b 2000000 -U
   ```

6. After `download success`, reset the board once to boot.

USB stays connected until the tool finishes. The SDK also has a Linux version, `uartfwburn.linux`.

## Flash with `--upload`

`--upload` and `--upload-port` on the build command flash `flash_ntz.bin` with the SDK's `uartfwburn` (`tools/Pro2_PG_tool _v1.3.0.zip`) when the build finishes:

```bash
./waf copter --upload --upload-port COM7          # WSL: runs the Windows tool through interop
./waf copter --upload --upload-port /dev/ttyUSB0  # Linux: runs the Linux tool
```

- When the build prints `請讓開發板進入燒錄模式` ("put the board into download mode"), put the chip into download mode. If it is not in download mode within 60 seconds, the build retries by itself; on timeout nothing is written. After `download success`, reset once to boot.
- `COMn` needs WSL interop (running Windows programs from WSL); without it, `--upload` says so before compiling starts. On WSL with systemd, after the distribution shuts down when idle and starts again in the same WSL VM, interop may not register again: `wsl --shutdown` on Windows restarts WSL, and a WSL terminal kept open while building and flashing keeps the distribution running.
- `/dev/ttyXXX` needs read and write access to the device (the `dialout` group); on WSL, the USB serial device is first attached to WSL with usbipd.
- The environment variable `AP_OVERRIDE_UPLOAD_CMD` replaces the command with your own: it is run with the image path appended, followed by `--port <port>` when `--upload-port` is given.
- It only downloads, without erasing, and it does not reset the board afterwards.

## Update over Wi-Fi

The board application `app/ota` takes a signed image over Wi-Fi, writes it to the other firmware slot and boots it on trial. The new image is kept once it confirms itself; otherwise the board goes back to the previous slot.

The first image is built as in [Build for update over Wi-Fi](building.md#build-for-update-over-wi-fi) and flashed through the USB serial port once, as above.

### Send an image

Each image sent over Wi-Fi is built like the first one: with `app/ota`, the same public key and the same `FLASH_LAYOUT`. Then sign it and send it:

```bash
python3 Tools/scripts/rtl8735b_ota_sign.py --private-key ~/mykey_private_key.dat build/<board type>/sdk_build/firmware_ntz.bin copter.ota
python3 Tools/scripts/rtl8735b_ota_send.py --host <board IP> copter.ota
python3 Tools/scripts/rtl8735b_ota_send.py --host <board IP> --probe
```

- `rtl8735b_ota_sign.py` signs `firmware_ntz.bin`, not `flash_ntz.bin`, and needs `pymonocypher` 3.1.3.2. With `--no-reboot`, the board does not reboot after writing the image.
- `rtl8735b_ota_send.py` needs no key. Before connecting, it checks the file's format, length, hash and checksum; with `--public-key <file>`, it also checks the signature. The board's IP is in `WiFi: up, local <IP>:<port>` on the ground station. The board listens on TCP port 8266 once `setup()` has finished and Wi-Fi has joined; where `setup()` stops, for example at `Config Error`, it does not listen. On a board without sensors, [`examples/AppHost`](board-applications.md#examplesapphost) built with `app/ota` runs the receiver.
- When the image is written, it prints `OTA 完成：N bytes 已寫入另一個韌體槽…` ("N bytes written to the other firmware slot"), and the board reboots into the new image unless it is armed.
- `--probe` writes nothing and reports the slot the board booted from: `fwN 已確認（不在試開中）` (confirmed) or `fwN 還在試開、等確認` (on trial, waiting for confirmation).

Images built another way:

- An image without `app/ota` runs with no trial and no receiver; going back takes flashing through the USB serial port.
- An image without the public key, or with another `FLASH_LAYOUT`, is never confirmed, and the board goes back after three trial boots.

### What the board does

- It refuses an image while armed, closing the connection without a reply; while the running image is on trial (`busy`); when the signature does not match its public key; when its partition table differs from the firmware's `FLASH_LAYOUT`; and when the image is larger than the limit in [Flash layout](flash-layout.md). Flash is untouched until the signature checks.
- It writes the image to the other slot as it arrives. From the first block, that slot's previous image is overwritten. At the end it compares the image's hash with the signed one and reads the image back; only then is the new slot enabled. If the hash differs or the connection drops, the other slot has no valid image, and the running slot is unchanged.
- During the transfer, each 4 KiB erase holds up the main loop until it finishes, and a normal arm is refused, see [Board applications](board-applications.md#functions-the-firmware-calls). On vehicles other than Copter, `ARMING_SKIPCHK` can skip that check, and a forced arm skips it on any vehicle; once armed, the board stops after at most one more 1 KiB block and one 4 KiB erase.
- The new image's first boot starts its trial, and each boot counts one trial boot. The image confirms itself once, at least 60 s after boot, its main loop runs, the receiver is listening with a public key and Wi-Fi is joined. Until then, a normal arm is refused, and the board refuses the next image.
- An image still on trial 300 s after boot is restarted: never while armed, and only 60 s after disarming. A crash that halts the scheduler, such as a HardFault, is not restarted; each power cycle counts one trial boot.
- After three trial boots without confirmation, the board goes back to the previous slot at boot.

### Recovering through the USB serial port

These cases are recovered only by flashing through the USB serial port:

- The new image crashes or hangs at boot before the board application starts: no trial boot is counted.
- The partition table differs from the firmware's layout and its slots reach beyond the flash, while the running image is on trial: arming and updates stay refused.
- An image with another `FLASH_LAYOUT` has used its trial boots, and the other slot has no valid image: it keeps running, with arming and updates refused.
- The trial count cannot be written at boot while the running image is on trial: arming and updates stay refused.
- The running image has no `app/ota`.

### Security limits

- The image is signed, but the transfer is plain TCP without a nonce. Anyone on the subnet who has a signed `.ota` file, or recorded one being sent, can send it again while the board is disarmed: to overwrite the other slot, to occupy the receiver again and again, refusing a normal arm while it lasts, or to install an older signed image. There is no protection against downgrades.
- Signed `.ota` files stay private, like the private key.
- The private key signs images that every board built with its public key accepts. It stays out of the repository and the firmware, and the build refuses a private key file. With the private key lost, those boards take new firmware only through the USB serial port.
- The probe needs no signature: anyone on the subnet can read which slot the board runs and whether it is confirmed.
- The Wi-Fi subnet is the trust boundary, as for MAVLink in [Wi-Fi](wifi.md#security-limits).

The status of manual flashing, of `--upload` and of update over Wi-Fi is in [Feature status](feature-status.md).
