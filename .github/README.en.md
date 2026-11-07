# ArduPilot RTL8735B port

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

繁體中文：[README.md](README.md)

This is an unofficial port of [ArduPilot](https://github.com/ArduPilot/ardupilot) to the Realtek RTL8735B, maintained by one person and unrelated to both the ArduPilot development team and Realtek. **It has not flown yet; do not use it for flight.**

This page gives the overall picture; the details of each feature are in the [HAL README][hal], and what changed in each version is in the [CHANGELOG][changelog].

## What works now

- The current version is **v0.2.1** (2026-11-08), compatible with v0.2.0.
- ArduPilot master `9f648ccabc` (2026-09-25) plus a HAL for the RTL8735B, with ArduPilot's full history kept.
- Board types: `hub8735ultra` (HUB 8735 ultra, vendor ideasHatch, [vendor resources](https://github.com/ideashatch/HUB-8735)), where all development happens; `amb82-mini` has only been built.
- **With a BMI160, BMP280 and GY-271 (QMC5883L) connected as in the example, the HUB 8735 ultra works:** it boots, connects to a ground station over Wi-Fi (UDP), keeps parameters across reboots, and the attitude moves the right way when you lift the board. The wiring, overlay and parameters are in the first example, `bmi160-bmp280-qmc5883l`, in the [examples repository][examples], and the board test record is T2 in [BOARD_TESTS.md][tests-board] (in Chinese).
- The I²C bus is cleared at boot: the occasional barometer initialization failure at boot in v0.1.0 did not come back over repeated warm and cold boots on a board (T3 in [BOARD_TESTS.md][tests-board]; the build tested predates giving each bus its own task).
- The Copter main loop runs at about 400 Hz on a board in normal operation (399.9 Hz averaged over 120 seconds, T4 in [BOARD_TESTS.md][tests-board]).
- v0.2.0 adds console MAVLink (a ground station through the board's USB serial port), analog volts, the microSD card, DShot with ESC telemetry and receive-only serial ports: all build and host tests only, not yet tried on a board.
- The board type itself has no sensors. With nothing connected, boot stops at the barometer Config Error; Wi-Fi joins the hotspot, but there is no MAVLink.
- Host tests that run without a board or the SDK: `Tools/scripts/rtl8735b_test.sh`, described in [tests/README.md][tests] (in Chinese). Passing host tests does not mean it works on a board.

For the status of each item, see [Feature status][status].

## What you can do with it

- **First time**: follow [Getting started][start] step by step, from wiring, building and flashing to a connected ground station.
- Details of building Copter and flashing it onto the board: [Getting and building][build], [Flashing][flash].
- Write an hwdef overlay for your own RTL8735B board (pins, buses, sensors): [Custom boards][custom]. Up to four I²C buses can be set up, for example a separate one for external sensors (not yet tried on a board).
- Connect an RC receiver (CRSF, ELRS and others) or a GPS: the wiring and the default serial ports are in [Board types][boards] (never tried with real devices).
- Build your own SDK-side C code into the firmware: [Board applications][app].
- Wire, build and load parameters from a ready-made example: the [examples repository][examples]. It has two examples so far: three sensor modules, and a hand-built expansion board (with headers for battery voltage, motors, ESC, GPS, RC receiver and a second I²C bus).
- Connect a ground station without Wi-Fi, through the board's USB serial port (the one used for flashing): console MAVLink under [Board types][boards] (not yet tried on a board).
- Measure the battery voltage, write logs to a microSD card, use DShot ESCs and ESC telemetry: [Custom boards][custom], [SD card][sd], [Motor outputs][motors] (none yet tried on a board).

## What is missing

- It has not flown.
- Not yet confirmed on a board:
  - compass orientation and calibration: so far it is only confirmed that the compass is detected;
  - the sign of roll: inferred from the pitch result, not tested directly;
  - whether parameters survive power loss: only reboots were tested;
  - several I²C buses in use at once;
  - UART, PWM output, Wi-Fi TCP, RC input, GPS, `--upload`, AMB82-mini;
  - everything new in v0.2.0: console MAVLink, analog volts, the SD card, DShot and ESC telemetry, receive-only serial ports, Wi-Fi packing and retries, stopping outputs when the main loop stalls, the arming block during a parameter erase, and the new board application interface;
  - the fixes in v0.2.1: ADC factory calibration, analog inputs returning 0 before their first sample, the IO and Storage threads starting early, and text kept off SERIAL0 with the Wi-Fi messages sent as STATUSTEXT.
- The IMU orientation in the first example of the examples repository was measured with the example board's mounting. If you mount it differently, test again; how is described in that example's README.
- Not done: bidirectional DShot, DShot1200, 3D with DShot; the chip's own USB (the board's other USB-C) does not carry MAVLink.
- On the first boot after the parameters move to the other sector, losing power or resetting during the erase returns all parameters to their defaults. Keep a parameter file from the ground station; see [Flash layout][layout].

## Getting involved

- Questions and board results are welcome as issues. Report them here, not to ArduPilot. Results for the items under "What is missing" above are especially useful, even if it is just "my board boots".
- If you want to send a PR, read [CONTRIBUTING](CONTRIBUTING.en.md) first, and run `Tools/scripts/rtl8735b_test.sh` once before sending it. PRs run the same tests; for PRs from outside contributors, the tests start only after the maintainer approves them.

## Recent changes

### v0.2.1 (2026-11-08): analog input and boot fixes, no text on SERIAL0

Compatible with v0.2.0: existing overlays, commands and parameters keep working. The changes take effect only after you build and flash again, and all of them are build and host tests only, not yet tried on a board. The tag is `v0.2.1` (`git checkout v0.2.1`); it only marks a commit, and no firmware is attached.

- **Changed: SERIAL0 (LOG UART) is taken over during HAL initialization, and text after that is no longer sent.** A serial terminal shows only the first few boot lines; the Wi-Fi connection messages (with the board's IP) go to the ground station as STATUSTEXT, so read them on a ground station connected to the COM port, see step 7 of [Getting started][start].
- Changed: analog inputs apply the chip's ADC factory calibration.
- Fixed: analog inputs return 0, not NaN, before their first sample; that used to be recorded as an internal error that blocked arming.
- Fixed: the IO and Storage threads start before `setup()`; every parameter save in `setup()` used to wait 2 seconds.
- Added: serial ports support parity and two stop bits (SBUS works through an external inverter); the generic board builds in external I²C barometer probing and airspeed, enabled by parameters.

The previous version, v0.2.0 (2026-10-22), added console MAVLink, analog volts, the microSD card, DShot and ESC telemetry, with two incompatible changes: the example overlay moved to the examples repository, and SERIAL0 takes PF3/PF4 by default. The full changes of every version are in the [CHANGELOG][changelog].

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
[sd]: /libraries/AP_HAL_RTL8735B/README.en.md#sd-card
[motors]: /libraries/AP_HAL_RTL8735B/README.en.md#motor-outputs
[examples]: https://github.com/rainwu0/ArduPilot-Hub8735Ultra
