# ArduPilot RTL8735B port

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

繁體中文：[README.md](README.md)

This is an unofficial port of [ArduPilot](https://github.com/ArduPilot/ardupilot) to the Realtek RTL8735B, maintained by one person and unrelated to both the ArduPilot development team and Realtek. **It has not flown yet; do not use it for flight.**

This page gives the overall picture; the details of each feature are in the [HAL README][hal], and what changed in each version is in the [CHANGELOG][changelog].

## What works now

- The current version is **v0.1.1** (2026-10-18), compatible with v0.1.0.
- ArduPilot master `9f648ccabc` (2026-09-25) plus a HAL for the RTL8735B, with ArduPilot's full history kept.
- Board types: `hub8735ultra` (HUB 8735 ultra, vendor ideasHatch, [vendor resources](https://github.com/ideashatch/HUB-8735)), where all development happens; `amb82-mini` has only been built.
- **With a BMI160, BMP280 and GY-271 (QMC5883L) connected as in the example, the HUB 8735 ultra works:** it boots, connects to a ground station over Wi-Fi (UDP), keeps parameters across reboots, and the attitude moves the right way when you lift the board. The wiring and the overlay are in [Connecting sensors][sensors], and the board test record is T2 in [BOARD_TESTS.md][tests-board] (in Chinese).
- The I²C bus is cleared at boot: the occasional barometer initialization failure at boot in v0.1.0 did not come back over repeated warm and cold boots on a board.
- The board type itself has no sensors. With nothing connected, boot stops at the barometer Config Error; Wi-Fi joins the hotspot, but there is no MAVLink.
- Host tests that run without a board or the SDK: `Tools/scripts/rtl8735b_test.sh`, described in [tests/README.md][tests] (in Chinese). Passing host tests does not mean it works on a board.

For the status of each item, see [Feature status][status].

## What you can do with it

- **First time**: follow [Getting started][start] step by step, from wiring, building and flashing to a connected ground station.
- Details of building Copter and flashing it onto the board: [Getting and building][build], [Flashing][flash].
- Write an hwdef overlay for your own RTL8735B board (pins, buses, sensors): [Custom boards][custom]. Up to four I²C buses can be set up, for example a separate one for external sensors (not yet tried on a board).
- Connect an RC receiver (CRSF, ELRS and others) or a GPS: the wiring and the default serial ports are in [Board types][boards] (never tried with real devices).
- Build your own SDK-side C code into the firmware: [Board applications][app].

## What is missing

- It has not flown.
- Not yet confirmed on a board:
  - compass orientation and calibration: so far it is only confirmed that the compass is detected;
  - the sign of roll: inferred from the pitch result, not tested directly;
  - whether parameters survive power loss: only reboots were tested;
  - the actual main loop rate;
  - several I²C buses in use at once;
  - UART, PWM output, Wi-Fi TCP, RC input, GPS, `--upload`, AMB82-mini.
- The IMU orientation in the example overlay was measured with the example board's mounting. If you mount it differently, test again; how is described in Connecting sensors.
- Not done: voltage conversion (so analog battery monitoring cannot be used), DShot, USB MAVLink, SD card.
- On the first boot after the parameters move to the other sector, losing power or resetting during the erase returns all parameters to their defaults. Keep a parameter file from the ground station; see [Flash layout][layout].

## Getting involved

- Questions and board results are welcome as issues. Report them here, not to ArduPilot. Results for the items under "What is missing" above are especially useful, even if it is just "my board boots".
- If you want to send a PR, read [CONTRIBUTING](CONTRIBUTING.en.md) first, and run `Tools/scripts/rtl8735b_test.sh` once before sending it. PRs run the same tests; for PRs from outside contributors, the tests start only after the maintainer approves them.

## Recent changes

### v0.1.1 (2026-10-18): steadier boot, a second I²C bus

Compatible with v0.1.0: existing overlays, commands and parameters keep working. Both fixes take effect only after you build and flash again. The tag is `v0.1.1` (`git checkout v0.1.1`); it only marks a commit, and no firmware is attached.

- Fixed: at boot the I²C bus is cleared before the controller is initialized, and the interrupt is masked while it is reinitialized. The occasional `Config Error: Baro` at boot in v0.1.0 did not come back over repeated warm and cold boots on a board.
- Fixed: when the IMU stops delivering samples (a loose wire, for example), the main thread no longer busy-waits without end, so lower priority work such as Wi-Fi and UART keeps running. The ground station's MAVLink comes from the main loop, so it still stops while the main loop is stuck. Verified by host tests only, not yet on a board.
- Added: up to four I²C buses in one hwdef, each with its own timeouts and disabling; see [Custom boards][custom]. Build and host tests only, not yet tried on a board.
- Added documentation: how to wire an RC receiver (CRSF, ELRS and others; SBUS is not supported) and a GPS, and their default serial ports; see [Board types][boards]. With host tests; never tried with real devices.
- Added documentation: the erase window on the first boot after the parameters move to the other sector; see the last item under "What is missing" above.

The previous version, v0.1.0 (2026-10-18), was the first one: with the sensors connected as in the example, the HUB 8735 ultra boots and connects to a ground station. The full changes of every version are in the [CHANGELOG][changelog].

## License

The whole repository is distributed under GPL-3.0, see [COPYING.txt](/COPYING.txt): most ArduPilot and port files are GPL-3.0-or-later, and some upstream files have other compatible licenses, as stated in each file's header. The Realtek SDK needed for building has its own license terms; it is not in this repository, and you have to get it yourself. This repository does not provide firmware images. For details, see [License][license] in the HAL README.

[hal]: /libraries/AP_HAL_RTL8735B/README.en.md
[changelog]: /libraries/AP_HAL_RTL8735B/CHANGELOG.en.md
[status]: /libraries/AP_HAL_RTL8735B/README.en.md#feature-status
[build]: /libraries/AP_HAL_RTL8735B/README.en.md#getting-and-building
[boards]: /libraries/AP_HAL_RTL8735B/README.en.md#board-types
[flash]: /libraries/AP_HAL_RTL8735B/README.en.md#flashing
[custom]: /libraries/AP_HAL_RTL8735B/README.en.md#custom-boards
[layout]: /libraries/AP_HAL_RTL8735B/README.en.md#flash-layout
[app]: /libraries/AP_HAL_RTL8735B/README.en.md#board-applications
[license]: /libraries/AP_HAL_RTL8735B/README.en.md#license
[tests]: /libraries/AP_HAL_RTL8735B/tests/README.md
[tests-board]: /libraries/AP_HAL_RTL8735B/BOARD_TESTS.md
[start]: /libraries/AP_HAL_RTL8735B/GETTING_STARTED.en.md
[sensors]: /libraries/AP_HAL_RTL8735B/README.en.md#connecting-sensors
