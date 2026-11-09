# ArduPilot RTL8735B port

[繁體中文](README.zh-TW.md)

The hardware abstraction layer (HAL) that runs ArduPilot on the Realtek RTL8735B. A HUB 8735 ultra running this HAL has flown a 3.5-inch quadcopter; see [Feature status](docs/feature-status.md).

This is an unofficial port maintained by one person, unrelated to the ArduPilot development team. Problems with this platform are reported in [this repository's issues](https://github.com/rainwu0/ArduPilot-RTL8735B/issues).

## Board types

| Board type | Board | Board tests |
|---|---|---|
| [`hub8735ultra`](hwdef/hub8735ultra/README.md) | HUB 8735 ultra (ideasHatch) | Development and every board test so far use this board |
| [`hub8735ultra-expansion`](hwdef/hub8735ultra-expansion/README.md) | HUB 8735 ultra on its expansion board, with an IMU, a barometer and a compass | T5, the flights of this hardware |
| [`amb82-mini`](hwdef/amb82-mini/README.md) | AMB82-mini | See its [Status](hwdef/amb82-mini/README.md#status) |
| [`rtl8735b-empty`](hwdef/rtl8735b-empty/README.md) | None: the settings the other board types include | No board records of its own |

## Documentation

Tutorial:

- [Getting started](GETTING_STARTED.md): from a HUB 8735 ultra and two sensor modules to a ground station showing the board's attitude.

How-to guides:

- [Build the firmware](docs/building.md)
- [Flash the firmware](docs/flashing.md)
- [Describe your own board](docs/custom-boards.md): sensors, pins, buses, serial ports and the other overlay settings

Reference:

- [Feature status](docs/feature-status.md): the status of each feature (works, needs a step, or none)
- [hwdef settings](docs/hwdef.md)
- [Motor outputs](docs/motor-outputs.md): PWM, OneShot, brushed, DShot and ESC telemetry
- [Wi-Fi](docs/wifi.md)
- [SD card](docs/sd-card.md)
- [Flash layout](docs/flash-layout.md): partitions and parameter storage
- [Board applications](docs/board-applications.md): the interface for SDK-side C code built into the firmware
- [CHANGELOG](CHANGELOG.md)
- [BOARD_TESTS.md](BOARD_TESTS.md): the board test records (in Chinese)
- [tests/README.md](tests/README.md): the host tests (in Chinese)

Explanation:

- [Design notes](docs/design.md): why the port behaves as it does where that is not obvious

## Changes to ArduPilot shared code

- `ArduCopter/takeoff_check.cpp`: in builds without ESC telemetry (`HAL_WITH_ESC_TELEM` is 0, for example a custom board whose overlay has `undef HAL_SUPPORT_RCOUT_SERIAL`), the original pre-takeoff check is not compiled at all, and after arming the motors stay at ground idle. Changed so that without ESC telemetry only the motor RPM check is skipped and the CPU load check still runs (this HAL does not report system load yet, so for now it does not block); builds with ESC telemetry (the board types' default) are not affected and use the original upstream check. This repository takes the commit of upstream [PR 34070](https://github.com/ArduPilot/ardupilot/pull/34070) (by yuiseki) as it is; as of 2026-10-07 the PR has not been merged upstream.
- `libraries/AP_HAL/AP_HAL_Boards.h`: `HAL_BOARD_RTL8735B` is 100, clear of the platform numbers upstream assigns in sequence.
- `libraries/AP_HAL/board/rtl8735b.h` (a new file): this platform's defaults, such as the board name, memory class, parameter storage size, the SD card and log directory switches, the program size limit and the semaphore types, in the same place as other platforms' `libraries/AP_HAL/board/*.h`; it is included only on this platform and does not change the behavior of other platforms.
- `Tools/ardupilotwaf/boards.py` (waf board registration) and `libraries/AP_Filesystem/AP_Filesystem.h` (this platform's newlib has no `dirent.h`, so the definitions are added here): add this platform without changing the behavior of other platforms.
- `libraries/AP_Filesystem/AP_Filesystem.cpp` and `AP_Filesystem_config.h`: add this platform's SD card backend `AP_Filesystem_RTL8735B` (a new file) without changing the behavior of other platforms.

## License

This repository as a whole is distributed under GPL-3.0: most ArduPilot and port files are GPL-3.0-or-later, and some upstream files have other compatible licenses, as stated in each file's header. The Realtek SDK has its own license terms (`Realtek_Disclaimer-2019.pdf` in the SDK root); it is not in this repository and is obtained separately. The built firmware contains Realtek's precompiled libraries; this repository does not provide firmware images, and neither do its Releases or CI artifacts. Firmware you build yourself has to comply with both GPL-3.0 and Realtek's terms before you distribute it.

Several parts of this port are adapted from ArduPilot's ESP32 port (`libraries/AP_HAL_ESP32` and `Tools/ardupilotwaf/esp32.py` in ArduPilot `9f648ccabc`): `Semaphores.cpp`, `Semaphores.h`, `system.cpp`, `hwdef/scripts/rtl8735b_hwdef.py`, and the hwdef generation and cmake setup in `Tools/ardupilotwaf/rtl8735b.py`; each file's header says so. How the Scheduler is split into FreeRTOS tasks also follows the ESP32 port's design. Both are GPL-3.0-or-later.

The SD card filesystem backend `libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp` is adapted from `AP_Filesystem_FATFS.cpp` in ArduPilot `9f648ccabc`, as its header says. FatFs in `fatfs/` is under ChaN's FatFs license (BSD-style; see `fatfs/LICENSE.txt` and each file's header) and can be distributed together with GPL-3.0 code.
