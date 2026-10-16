# ArduPilot RTL8735B port

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

繁體中文：[README.md](README.md)

This is an unofficial port of [ArduPilot](https://github.com/ArduPilot/ardupilot) to the Realtek RTL8735B, maintained by one person and unrelated to both the ArduPilot development team and Realtek. **It has not flown yet; do not use it for flight.**

The details are all in the [HAL README][hal]; this page only gives the outline.

## What works now

- ArduPilot master `9f648ccabc` (2026-09-25) plus a HAL for the RTL8735B, with ArduPilot's full history kept.
- Board types: `hub8735ultra` (HUB 8735 ultra, vendor ideasHatch, [vendor resources](https://github.com/ideashatch/HUB-8735)), where all development happens; `amb82-mini` has only been built.
- **With a BMI160, BMP280 and GY-271 (QMC5883L) connected as in the example, the HUB 8735 ultra works:** it boots, connects to a ground station over Wi-Fi (UDP), keeps parameters across reboots, and the attitude moves the right way when you lift the board. The wiring and the overlay are in [Connecting sensors][sensors], and the board test record is T2 in [BOARD_TESTS.md][tests-board] (in Chinese).
- The board type itself has no sensors. With nothing connected, boot stops at the barometer Config Error; Wi-Fi joins the hotspot, but there is no MAVLink.
- Host tests that run without a board or the SDK: `Tools/scripts/rtl8735b_test.sh`, described in [tests/README.md][tests] (in Chinese). Passing host tests does not mean it works on a board.

For the status of each item, see [Feature status][status].

## What you can do with it

- **First time**: follow [Getting started][start] step by step, from wiring, building and flashing to a connected ground station.
- Details of building Copter and flashing it onto the board: [Getting and building][build], [Flashing][flash].
- Write an hwdef overlay for your own RTL8735B board (pins, buses, sensors): [Custom boards][custom].
- Build your own SDK-side C code into the firmware: [Board applications][app].

## What is missing

- It has not flown.
- Not yet confirmed on a board:
  - compass orientation and calibration: so far it is only confirmed that the compass is detected;
  - the sign of roll: inferred from the pitch result, not tested directly;
  - whether parameters survive power loss: only reboots were tested;
  - the actual main loop rate;
  - UART, PWM output, Wi-Fi TCP, RC input, GPS, `--upload`, AMB82-mini.
- The IMU orientation in the example overlay was measured with the example board's mounting. If you mount it differently, test again; how is described in Connecting sensors.
- Not done: voltage conversion (so analog battery monitoring cannot be used), DShot, USB MAVLink, SD card.
- Known issue: the barometer occasionally fails to initialize at boot, and the cause has not been found. It happened once in T2, and pressing reset to reboot fixed it.

## Getting involved

- Questions and board results are welcome as issues. Report them here, not to ArduPilot. Results for the items under "What is missing" above are especially useful, even if it is just "my board boots".
- If you want to send a PR, read [CONTRIBUTING](CONTRIBUTING.en.md) first, and run `Tools/scripts/rtl8735b_test.sh` once before sending it. PRs run the same tests; for PRs from outside contributors, the tests start only after the maintainer approves them.

## Recent changes

### v0.1.0: HUB 8735 ultra works with sensors connected

The first milestone: wired as in the example, the HUB 8735 ultra boots and connects to a ground station. This version is tagged `v0.1.0` (`git checkout v0.1.0`). The tag only marks a commit; no firmware is attached.

- Added [Getting started][start]: someone with no background can follow it from wiring to a connected ground station, and every step says what you see when it works.
- Added the example overlay `hwdef/hub8735ultra/sensors-example.dat` and the HAL README section Connecting sensors: the BMI160 on SPI, the BMP280 and GY-271 on I²C, with a wiring table.
- Added [BOARD_TESTS.md][tests-board] (in Chinese), recording every board test: version, image, wiring, steps, results and what was not tested. Every "Verified on a board" in the feature status table carries a record number.
- The home page, CONTRIBUTING, the HAL README and Getting started now also come in English, and each links to the other language at the top.
- SPI, I²C, scheduling, Wi-Fi MAVLink (UDP) and parameter storage moved from "To be rechecked" to "Verified on a board (T2)". Parameter storage was verified across reboots only.
- The limits are stated as they are under "What is missing" above: not flown yet, compass orientation and calibration not verified, roll direction inferred.

### 2026-10-14: public tests

- Added `Tools/scripts/rtl8735b_test.sh`: tool tests for hwdef, the Flash layout and `--upload`, and host tests for the HAL (serial, SPI/I²C, IO, RC input, parameter storage, scheduling, Wi-Fi). It needs `build-essential` and `python3`, but no board and no SDK. Add `--build` to also cross-build once.
- On pushes to `main` and on PRs, GitHub Actions runs the same tests and the commit convention check; the cross build is triggered by hand. The CI settings carried over from ArduPilot that are of no use here have been removed.

For older changes, see `git log 9f648ccabc..main`. Overlays from before 2026-10-14 need attention: the old `HAL_RTL8735B_*` pin settings have been replaced by keywords (see [Custom boards][custom]), using the same pin twice now fails at configure, and a board application without `rtl8735b_app_init()` fails to link.

## License

The whole repository is distributed under GPL-3.0, see [COPYING.txt](/COPYING.txt): most ArduPilot and port files are GPL-3.0-or-later, and some upstream files have other compatible licenses, as stated in each file's header. The Realtek SDK needed for building has its own license terms; it is not in this repository, and you have to get it yourself. This repository does not provide firmware images. For details, see [License][license] in the HAL README.

[hal]: /libraries/AP_HAL_RTL8735B/README.en.md
[status]: /libraries/AP_HAL_RTL8735B/README.en.md#feature-status
[build]: /libraries/AP_HAL_RTL8735B/README.en.md#getting-and-building
[flash]: /libraries/AP_HAL_RTL8735B/README.en.md#flashing
[custom]: /libraries/AP_HAL_RTL8735B/README.en.md#custom-boards
[app]: /libraries/AP_HAL_RTL8735B/README.en.md#board-applications
[license]: /libraries/AP_HAL_RTL8735B/README.en.md#license
[tests]: /libraries/AP_HAL_RTL8735B/tests/README.md
[tests-board]: /libraries/AP_HAL_RTL8735B/BOARD_TESTS.md
[start]: /libraries/AP_HAL_RTL8735B/GETTING_STARTED.en.md
[sensors]: /libraries/AP_HAL_RTL8735B/README.en.md#connecting-sensors
