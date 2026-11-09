# Flash the firmware

[繁體中文](flashing.zh-TW.md)

How to write `flash_ntz.bin` to an RTL8735B board through its USB serial port, by hand on Windows or with `--upload` at the end of a build. Building the image is in [Build the firmware](building.md).

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

The status of manual flashing and of `--upload` is in [Feature status](feature-status.md).
