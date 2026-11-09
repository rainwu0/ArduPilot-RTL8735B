# ArduPilot RTL8735B port

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

[繁體中文](README.zh-TW.md)

Unofficial ArduPilot port to the Realtek RTL8735B SoC. Docs in Traditional Chinese and English.

This repository is [ArduPilot](https://github.com/ArduPilot/ardupilot) with a hardware abstraction layer (HAL) for the Realtek RTL8735B. It is maintained by one person and is unrelated to both the ArduPilot development team and Realtek.

## Table of Contents

- [Security](#security)
- [Background](#background)
- [Install](#install)
- [Usage](#usage)
- [Status](#status)
- [Contributing](#contributing)
- [License](#license)

## Security

- MAVLink over Wi-Fi, UDP or TCP, has no authentication: any device on the same subnet can send to the board and be taken as its ground station. See the security limits in [Wi-Fi][wifi].
- The Wi-Fi credentials from a settings file are compiled into the firmware image, and anyone who has the image may be able to read them. Without a settings file, the board joins the [default hotspot][wifi-default] (`ardupilot`, `ardupilot1`).
- Security problems are reported privately, as described in [SECURITY](SECURITY.md).

## Background

- ArduPilot master `9f648ccabc` (2026-09-25) plus a HAL for the RTL8735B, with ArduPilot's full history kept. The HAL is in `libraries/AP_HAL_RTL8735B/`; changes to ArduPilot's shared code are listed in the [HAL README][hal].
- Board types: `hub8735ultra` for the HUB 8735 ultra from ideasHatch ([vendor resources](https://github.com/ideashatch/HUB-8735)), where all development and board tests happen; `hub8735ultra-expansion`, the HUB 8735 ultra on its expansion board with an IMU, a barometer and a compass; `amb82-mini`; and `rtl8735b-empty`, the settings the others include. Each has a README under [`hwdef/`][boards].
- Apart from `hub8735ultra-expansion`, the board types have no sensors. Sensors, their mounting and default parameters go in an overlay hwdef. Ready-made overlays with wiring and parameters are in the [examples repository][examples].

## Install

Building runs on Ubuntu 22.04, or Ubuntu 22.04 in WSL, and needs the Realtek RTL8735B SDK, which a script fetches:

```bash
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$PWD/../ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
./waf configure --board hub8735ultra
./waf copter
```

The details, including where to clone in WSL and the Wi-Fi settings file, are in [Build the firmware][build]. This repository does not provide firmware images.

## Usage

- **First time**: follow [Getting started][start] step by step, from wiring two sensor modules, building and flashing, to a ground station showing the board's attitude.
- Flash an image: [Flash the firmware][flash].
- Write an overlay for your own board, sensors, pins and buses: [Describe your own board][custom].
- Connect a ground station over Wi-Fi ([Wi-Fi][wifi]) or through the board's USB serial port (SERIAL0 in the [HUB 8735 ultra README][hub]).
- Connect an RC receiver or a GPS: the HUB 8735 ultra README; motors and ESCs: [Motor outputs][motors].
- Build your own SDK-side C code into the firmware: [Board applications][app].
- Everything else, by type of document: the [HAL README][hal].

## Status

| Item | Status |
|---|---|
| Flying a quadcopter: HUB 8735 ultra with an expansion board ([`hub8735ultra-expansion`][expansion]), ground station over Wi-Fi, DShot300 | Works (T5) |
| HUB 8735 ultra with a BMI160, a BMP280 and a GY-271 (QMC5883L) wired as in the examples repository, ground station over Wi-Fi (UDP) | Works (T2) |
| Parameters in flash | Works (T2, T5) |
| Battery voltage and current | Needs: calibration |
| Compass | Needs: orientation check and calibration |
| RC receiver, GPS | Needs: one try with the device |
| ESC telemetry, DShot150, DShot600, other PWM outputs | Needs: one try with ESCs |
| SD card and logs | Needs: one try with a card |
| MAVLink on the USB serial port, Wi-Fi TCP, the default Wi-Fi hotspot, `--upload` | Needs: one try on a board |
| AMB82-mini | Needs: one try on a board |
| Firmware update over Wi-Fi | None |

The records are in [BOARD_TESTS.md][tests-board] (in Chinese). The status of each HAL feature is in [Feature status][status], and that of each board under *Specifications* in its README; what changed in each version is in the [CHANGELOG][changelog].

Host tests run without a board or the SDK: `Tools/scripts/rtl8735b_test.sh`, described in [tests/README.md][tests] (in Chinese). Passing host tests does not mean it works on a board.

## Contributing

- Questions and board results are welcome as [issues](https://github.com/rainwu0/ArduPilot-RTL8735B/issues), in Chinese or English. Results for items marked Needs are especially useful, even if it is just "my board boots".
- Pull requests: [CONTRIBUTING](CONTRIBUTING.md).

## License

GPL-3.0, see [COPYING.txt](/COPYING.txt). Copyright belongs to the authors of each file: the ArduPilot contributors, and the contributors to this port. Most ArduPilot and port files are GPL-3.0-or-later, and some upstream files have other compatible licenses, as stated in each file's header. The Realtek SDK needed for building has its own license terms; it is not in this repository and is obtained separately. For details, see *License* in the [HAL README][hal].

[hal]: /libraries/AP_HAL_RTL8735B/README.md
[changelog]: /libraries/AP_HAL_RTL8735B/CHANGELOG.md
[start]: /libraries/AP_HAL_RTL8735B/GETTING_STARTED.md
[status]: /libraries/AP_HAL_RTL8735B/docs/feature-status.md
[build]: /libraries/AP_HAL_RTL8735B/docs/building.md
[flash]: /libraries/AP_HAL_RTL8735B/docs/flashing.md
[custom]: /libraries/AP_HAL_RTL8735B/docs/custom-boards.md
[wifi]: /libraries/AP_HAL_RTL8735B/docs/wifi.md
[wifi-default]: /libraries/AP_HAL_RTL8735B/docs/wifi.md#default-hotspot
[motors]: /libraries/AP_HAL_RTL8735B/docs/motor-outputs.md
[app]: /libraries/AP_HAL_RTL8735B/docs/board-applications.md
[boards]: /libraries/AP_HAL_RTL8735B/hwdef/
[hub]: /libraries/AP_HAL_RTL8735B/hwdef/hub8735ultra/README.md
[expansion]: /libraries/AP_HAL_RTL8735B/hwdef/hub8735ultra-expansion/README.md
[tests]: /libraries/AP_HAL_RTL8735B/tests/README.md
[tests-board]: /libraries/AP_HAL_RTL8735B/BOARD_TESTS.md
[examples]: https://github.com/rainwu0/ArduPilot-Hub8735Ultra
