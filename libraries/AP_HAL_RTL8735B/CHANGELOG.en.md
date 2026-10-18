# Changelog

繁體中文：[CHANGELOG.md](CHANGELOG.md)

All notable changes to this port are recorded here, newest first. The format follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/); versions follow [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html), and before 1.0.0 the [Cargo 0.y.z convention](https://doc.rust-lang.org/1.90.0/cargo/reference/semver.html): incompatible changes increase the middle number, all other changes the last one.

The status of each feature in a given version (verified on a board, build only and so on) is what *Feature status* in that version's [README](README.en.md) says; board test records are in [BOARD_TESTS.md](BOARD_TESTS.md) (in Chinese).

## [Unreleased]

## [0.1.1] - 2026-10-19

Compatible with v0.1.0: existing overlays, commands and parameters keep working.

### Fixed

- I²C: at boot the bus is cleared before the controller is initialized, and the I²C interrupt is masked while the controller is reinitialized. The occasional `Config Error: Baro: unable to initialise driver` at boot in v0.1.0 did not come back over repeated warm and cold boots on a board (T3 in BOARD_TESTS; the build tested predates giving each bus its own task, and the v0.1.1 code itself has not been on a board).
- Scheduling: when the IMU stopped delivering samples (a loose wire, for example), the main thread used to busy-wait without end, and the lower priority Wi-Fi, UART and IO tasks never ran. Now the main thread yields for one tick after busy-waiting for 20 ms, so those tasks keep running. The MAVLink a ground station receives comes from the main loop, so it still stops while the main loop is stuck waiting for the IMU. Verified by host tests only; an IMU that delivers no samples at all has not been tried on a board.

### Added

- Up to four I²C buses in one hwdef (`RTL8735B_I2CBUS`, one for each of SDK I2C0–I2C3). Each bus has its own bus task, timeout count and disabling, so the others carry on when one is disabled. Overlays with a single bus keep working. Build and host tests only, not yet tried on a board.
- README: how to wire an RC receiver and a GPS, and their default serial ports (RC input on SERIAL4, GPS on SERIAL3). Serial RC protocols that use 8N1 without inversion work, for example CRSF and ELRS; SBUS is not supported. With host tests; never tried with real devices.
- README: on the first boot after the parameters move to the other sector, losing power or resetting during the erase returns all parameters to their defaults; keep a parameter file from the ground station. With host tests.

## [0.1.0] - 2026-10-18

The first version with a version number: ArduPilot master `9f648ccabc` (2026-09-25) plus the RTL8735B HAL.

### Added

- The RTL8735B HAL: scheduling and semaphores, SPI, I²C (after a transfer times out the controller is reinitialized and the bus cleared if needed; the bus is disabled only after 3 timeouts in a row), UART, Wi-Fi MAVLink (UDP, or TCP chosen at build time), serial RC input, GPS, PWM output (normal PWM, OneShot, OneShot125, brushed), raw AnalogIn samples, and parameters stored in Flash.
- Board types `hub8735ultra` (HUB 8735 ultra), `amb82-mini` (only built) and `rtl8735b-empty`.
- hwdef keywords for custom boards: `RTL8735B_I2CBUS`, `RTL8735B_SPIBUS`, `RTL8735B_SPIDEV`, `RTL8735B_RCOUT`, `RTL8735B_GPIO`, `RTL8735B_ADC`, added with `--extra-hwdef`.
- Flash layouts `realtek-ota` (default), `sdk-d1b6426` and `arduino-default`, chosen with `FLASH_LAYOUT`.
- Board applications: `env RTL8735B_APP_DIR` and `rtl8735b_app_init()`.
- Flashing after the build with `--upload` (not yet verified on a board).
- The example overlay `hwdef/hub8735ultra/sensors-example.dat`: a BMI160 on SPI, and a BMP280 and GY-271 (QMC5883L) on I²C.
- Host tests that need no board and no SDK, `Tools/scripts/rtl8735b_test.sh`; on pushes to `main` and on PRs, GitHub Actions runs the same tests and the commit convention check.
- The getting started guide, the board test records BOARD_TESTS.md, and English versions of the home page, CONTRIBUTING, the HAL README and the getting started guide.

### Changed

Compared with the public commits before this version number:

- When two settings use the same pin (an enabled serial port counts), configure fails and names both sources of the conflict.
- When an overlay sets `RTL8735B_APP_DIR` but the application does not define `rtl8735b_app_init()`, linking fails.

### Removed

- The old `HAL_RTL8735B_*` pin and bus `define`s (`HAL_RTL8735B_I2C1_*`, `HAL_RTL8735B_SPI1_*`, `HAL_RTL8735B_RCOUT_*` and others); use the keywords above. Overlays that still use them build without errors, but the settings have no effect.

[Unreleased]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/rainwu0/ArduPilot-RTL8735B/tree/v0.1.0
