# SD card

[繁體中文](sd-card.zh-TW.md)

The board's microSD card is read and written through FatFs (`libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp`), and the ground station's MAVLink FTP sees the files on the card.

## Settings

- The board type `rtl8735b-empty` turns it on (`define HAL_RTL8735B_SDCARD_ENABLED 1` in its hwdef), and so do `hub8735ultra` and `amb82-mini`, which include it, and `hub8735ultra-expansion`, which includes `hub8735ultra`.
- A custom board without an SD card slot turns it off with `undef HAL_RTL8735B_SDCARD_ENABLED` in its overlay.

## Cards and mounting

- The card is formatted as FAT32 or exFAT on a computer first. A card without a filesystem can also be formatted from the ground station with the MAVLink command `MAV_CMD_STORAGE_FORMAT`, which erases the whole card.
- The card is mounted the first time a file is accessed after boot. Without a card, mounting is retried at most once every 3 seconds; after a card is inserted, the next access mounts it.
- The SD signals stay at 3.3 V; the 1.8 V high-speed modes are not used.

## Logging

Filesystem logging is built into the firmware but not written by default (`LOG_BACKEND_TYPE` defaults to 0). With `LOG_BACKEND_TYPE` set to 1, logs go to `/APM/LOGS` on the card. With that set and no card inserted, the pre-arm checks report `Logging failed` or `No SD card`.

## FatFs

FatFs is in `fatfs/`: R0.14b, taken from ArduPilot's ChibiOS submodule (`ext/fatfs` at commit `9aebaf4a40`, which `modules/ChibiOS` points to), with the same content as ChaN's R0.14b release. `fatfs/ffconf.h` is this platform's configuration, taken from ArduPilot's `libraries/AP_HAL_ChibiOS/hwdef/common/ffconf.h`.

The status of SD card support is in [Feature status](feature-status.md).
