# Changelog

<!-- markdownlint-configure-file { "MD024": { "siblings_only": true } } -->

[繁體中文](CHANGELOG.zh-TW.md)

All notable changes to this port are recorded here, newest first. The format follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/); versions follow [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html), and before 1.0.0 the [Cargo 0.y.z convention](https://doc.rust-lang.org/1.90.0/cargo/reference/semver.html): incompatible changes increase the middle number, all other changes the last one.

The status of each feature in a given version is what that version's feature status says: [docs/feature-status.md](docs/feature-status.md), or up to v0.2.0 the *Feature status* section of the HAL README. Board test records are in [BOARD_TESTS.md](BOARD_TESTS.md) (in Chinese).

## [Unreleased]

Compatible with v0.2.1: existing overlays, commands and parameters keep working; the changes need a new build and flash.

### Changed

- **Without a Wi-Fi settings file, the three board types now join a default hotspot**, SSID `ardupilot` and password `ardupilot1`; before, Wi-Fi was off. The ground station receives `WiFi: joining the board's default hotspot (SSID length N)`, and the build message for an unset `RTL8735B_WIFI_CONFIG` names the default hotspot. See [Default hotspot](docs/wifi.md#default-hotspot).
- When Wi-Fi cannot join, the retries no longer send STATUSTEXT each time: the ground station receives joining once, and `WiFi: join failed (…)` the first time, when the error code changes, and at most once every 30 s after that; the rest only goes to the console. After the board has joined, a later link loss starts the count again. See [Messages](docs/wifi.md#messages).
- Documentation: [Wi-Fi](docs/wifi.md) has a *Default hotspot* section, and Getting started can use the default hotspot instead of a settings file.
- Getting started and the HUB 8735 ultra README build the three sensor modules with the board type `hub8735ultra-expansion`, instead of `hub8735ultra` with the overlay from the examples repository, which is no longer cloned. [Describe your own board](docs/custom-boards.md) points at the sensor lines of `hub8735ultra-expansion`.
- Documentation: the home page lists what the project is made up of, and its status table has a row for firmware built from this repository on the expansion board. *What next* in Getting started leads to the expansion board README and ArduPilot's First Flight.

### Added

- The board type `hub8735ultra-expansion`: the HUB 8735 ultra on an expansion board with a BMI160, a BMP280 and a QMC5883L, battery voltage on analog pin 2 and ESC current on analog pin 9, SERIAL3 receive-only for ESC telemetry, battery monitoring and GPS on SERIAL1 as defaults in `hwdef.dat`, and DShot300 in `defaults.parm`. See [its README](hwdef/hub8735ultra-expansion/README.md).
- `Tools/Frame_params/hub8735ultra-expansion-quad-3.5in-2S.param`: parameters for a 3.5-inch quad X with a 4-in-1 Bluejay ESC on DShot300 and a 2S battery.
- Documentation: the README of `hub8735ultra-expansion`, with the expansion board's connectors, the sensor modules, a wiring diagram, a picture and a video of the T5 quadcopter, and its parameters.
- Update over Wi-Fi: the board application `app/ota` takes an image signed with a key from ArduPilot's `generate_keys.py`, on TCP port 8266, writes it to the other firmware slot and boots it on trial; after three trial boots without confirmation it goes back to the previous slot. `Tools/scripts/rtl8735b_ota_sign.py` signs `firmware_ntz.bin`, and `Tools/scripts/rtl8735b_ota_send.py` sends it or asks which slot runs. `RTL8735B_OTA_PUBLIC_KEY` names the public key at build time. See [Update over Wi-Fi](docs/flashing.md#update-over-wi-fi).
- Board application interface: `rtl8735b_app_arming_blocked`; while it returns non-zero, a normal arm is refused. See [Board applications](docs/board-applications.md).

## [0.2.1] - 2026-11-10

Compatible with v0.2.0: existing overlays, commands and parameters keep working; the changes need a new build and flash.

### Changed

- **SERIAL0 (LOG UART) is taken over during HAL initialization, and after that, text that the SDK and the HAL print with printf is no longer sent**, so it does not land between MAVLink packets; a serial terminal shows only the first few boot lines. The HAL's runtime messages go to the ground station as STATUSTEXT, and so do the Wi-Fi connection messages: `WiFi: joining (SSID length N)`, `WiFi: up, local <IP>:<port>`, `WiFi: join failed (…)`, `WiFi: no STA credentials in this image; WiFi MAVLink port disabled`. The board's IP is now read from a ground station on the COM port, and steps 7 and 8 of Getting started change with it. To see crash dumps, turn SERIAL0 off in an overlay.
- Analog inputs apply the chip's ADC factory calibration: when the chip has calibration parameters, a reading is corrected before it is converted to volts; without them it is used as before.
- Documentation file names: the English version of each document now has the default name and the Traditional Chinese version ends in `.zh-TW.md`. `README.en.md`, `GETTING_STARTED.en.md`, `CHANGELOG.en.md` and `.github/CONTRIBUTING.en.md` are now `README.md`, `GETTING_STARTED.md`, `CHANGELOG.md` and `.github/CONTRIBUTING.md`; the Chinese versions that had those names are now `README.zh-TW.md`, `GETTING_STARTED.zh-TW.md`, `CHANGELOG.zh-TW.md` and `.github/CONTRIBUTING.zh-TW.md`. The GitHub home page shows English by default.
- Documentation layout: the HAL README keeps the board types, an index of the documents, the changes to ArduPilot shared code and the license. Its other sections move to `docs/`: `building.md` (Getting and building), `flashing.md` (Flashing), `custom-boards.md` and `hwdef.md` (Custom boards), `board-applications.md`, `flash-layout.md`, `motor-outputs.md`, `wifi.md`, `sd-card.md` and `feature-status.md`, each in English and Chinese.
- Documentation: the HUB 8735 ultra pins, buses, serial ports, PWM outputs, GPIOs, analog pins, sensor wiring and flashing move from the HAL README to `hwdef/hub8735ultra/README.md`, in the sections of an ArduPilot board README, with a figure of the 32 header pins (`pinout.png`, `pinout.svg`).
- Documentation: feature status is a status table, where each feature works (with its board test record), needs a named step, or is not there; the home page and each board README have a status table of their own. The home page and the HAL README give the flight status from T5, and Getting started no longer mentions it. The entries in this changelog no longer give each feature's status.

### Added

- Serial ports support no/odd/even parity and 1 or 2 stop bits (only 8N1 before), and a format change takes effect at once. SBUS (8E2) can be used as RC input through an external inverter; the chip's UART has no inversion in normal mode and no half duplex.
- The generic board builds in external I²C barometer probing and airspeed support: `BARO_PROBE_EXT` and `ARSPD_TYPE` turn them on, both default to off, and boot behaviour is unchanged. Automatic probing for external compasses is not turned on by default (it would override the orientation in an overlay's `COMPASS` lines).
- Documentation: READMEs for the board types `amb82-mini` and `rtl8735b-empty`; `docs/design.md`, on why the port behaves as it does; `.github/SECURITY.md`, on how to report security problems.
- Documentation: board test record T5 in `BOARD_TESTS.md` (in Chinese): a HUB 8735 ultra with an expansion board flew a 3.5-inch quadcopter on 2026-10-25 and 2026-11-06, controlled from a ground station over Wi-Fi MAVLink, with DShot300 to a 4-in-1 ESC.

### Fixed

- Analog inputs return 0, not NaN, before their first sample. The first battery voltage and current read at boot used to be NaN, which was recorded as the internal error `constraining_nan` and blocked arming until a reboot.
- The IO and Storage threads start before `setup()`. Every parameter save in `setup()` used to wait for its 2-second timeout (`setup()` took about 55 seconds on a board without motors), and saves made in a `setup()` that never finished were not written to flash.
- README: the changes to ArduPilot shared code list `libraries/AP_HAL/board/rtl8735b.h`.

## [0.2.0] - 2026-10-22

Two changes are incompatible with v0.1.1: the example overlay moves to the examples repository, and PF3 and PF4 now belong to SERIAL0 by default (the first two items below). Other overlays, commands and parameters keep working; the new features in this version need a new build and flash.

### Removed

- **The example overlay `hwdef/hub8735ultra/sensors-example.dat` moves to the examples repository.** A build that names this file, as in the v0.1.1 README or Getting started, no longer finds it; use `examples/bmi160-bmp280-qmc5883l/overlay.dat` in the examples repository [rainwu0/ArduPilot-Hub8735Ultra](https://github.com/rainwu0/ArduPilot-Hub8735Ultra) instead, which has the same sensor settings. Settings for one particular board belong in the examples repository; the HAL repository keeps only the board types.

### Changed

- **SERIAL0 (LOG UART, PF3 and PF4) is on by default, running MAVLink2 at 115200 baud.** An overlay that uses PF3 or PF4 for something else (a GPIO, for example) now fails to build; turn SERIAL0 off first (`undef HAL_RTL8735B_SERIAL0_ENABLED`, then `define HAL_RTL8735B_SERIAL0_ENABLED 0`). Once ArduPilot uses this port, the SDK console no longer takes commands.
- When the main loop has not completed a cycle for more than 100 ms while armed, all PWM outputs stop (ESCs see no signal and stop) and DShot sends zero throttle; when the main loop resumes, only the channels the main code writes again go out, with the new values.
- While the parameter area is erased at run time (a second sector change in the same boot, or a rebuild after a write failure), the arming check reports `Param storage failed` and a normal arm is refused until the erase finishes; a forced arm skips this check.
- Wi-Fi UDP to the ground station packs several MAVLink messages into one datagram, so each message can go out up to about 10 ms later; when the driver is out of packet buffers, the datagram is retried for up to 500 ms.
- Runtime warnings (output mode, PWM clock, Wi-Fi link loss and others) are printed on the console as before and also sent to the ground station as STATUSTEXT.
- The build no longer compiles the SDK's `ethernet_api.c` (GPL-2.0-only; this port does not use Ethernet, and the linker already discarded it).

### Added

- Console MAVLink: a ground station connects through the board's USB serial port (the one used for flashing) at 115200 baud, with no Wi-Fi needed. SDK and HAL text messages land between packets; disconnect the ground station from this port before flashing. The chip's own USB does not carry MAVLink.
- Analog inputs are converted to volts (12-bit, 3.3 V full scale), so analog battery monitoring can be used (with the divider ratio in `BATT_VOLT_MULT`); a hwdef can list several analog pins (PF0–PF3, PA0–PA3), each sampled on its own.
- Receive-only serial ports: set the TX pin to `PIN_NC`, for example for ESC telemetry, which uses RX only, freeing the TX pin as an analog input.
- DShot150, DShot300 and DShot600 (`MOT_PWM_TYPE` 4–6): the CPU sends the frames by writing GPIO, all channels at once, at 800 Hz or more; DShot commands and the BLHeli_S bit widths are supported. Bidirectional DShot, DShot1200 and 3D are not.
- ESC telemetry: the ESC's TLM wire goes to the RX of a serial port (`SERIALn_PROTOCOL` 16); current and voltage can serve as a battery monitor (`BATTn_MONITOR` 9) and are logged in `ESC` records.
- microSD card: read and written through FatFs (R0.14b), with the files on the card visible through MAVLink FTP in the ground station; with `LOG_BACKEND_TYPE` 1 logs go to the card (off by default). On by default in board type `rtl8735b-empty` and the board types that include it; a custom board without a card slot turns it off in its overlay.
- Board application interface (`app/rtl8735b_app.h`): calls after `setup()` finishes and before the firmware reboots, queries for the armed state and the main-loop time, and a way to make the outputs safe; applications also get the addresses of the flash layout. A board without sensors can run applications with the example `examples/AppHost`.
- README: DShot and ESC telemetry, console MAVLink, the SD card, several analog pins and receive-only serial ports, and how Wi-Fi sends.

### Fixed

- The description of `Tools/scripts/rtl8735b_get_sdk.sh`: the SDK location is given as the first argument; `AMEBAPRO2_SDK` is what waf reads.

## [0.1.1] - 2026-10-19

Compatible with v0.1.0: existing overlays, commands and parameters keep working.

### Fixed

- I²C: for the occasional `Config Error: Baro: unable to initialise driver` at boot in v0.1.0, the bus is now cleared at boot before the controller is initialized, and the I²C interrupt is masked while the controller is reinitialized.
- Scheduling: when the IMU stopped delivering samples (a loose wire, for example), the main thread used to busy-wait without end, and the lower priority Wi-Fi, UART and IO tasks never ran. Now the main thread yields for one tick after busy-waiting for 20 ms, so those tasks keep running. The MAVLink a ground station receives comes from the main loop, so it still stops while the main loop is stuck waiting for the IMU.

### Added

- Up to four I²C buses in one hwdef (`RTL8735B_I2CBUS`, one for each of SDK I2C0–I2C3). Each bus has its own bus task, timeout count and disabling, so the others carry on when one is disabled. Overlays with a single bus keep working.
- README: how to wire an RC receiver and a GPS, and their default serial ports (RC input on SERIAL4, GPS on SERIAL3). Serial RC protocols that use 8N1 without inversion are supported, for example CRSF and ELRS; SBUS is not supported.
- README: on the first boot after the parameters move to the other sector, losing power or resetting during the erase returns all parameters to their defaults; keep a parameter file from the ground station.

## [0.1.0] - 2026-10-18

The first version with a version number: ArduPilot master `9f648ccabc` (2026-09-25) plus the RTL8735B HAL.

### Added

- The RTL8735B HAL: scheduling and semaphores, SPI, I²C (after a transfer times out the controller is reinitialized and the bus cleared if needed; the bus is disabled only after 3 timeouts in a row), UART, Wi-Fi MAVLink (UDP, or TCP chosen at build time), serial RC input, GPS, PWM output (normal PWM, OneShot, OneShot125, brushed), raw AnalogIn samples, and parameters stored in Flash.
- Board types `hub8735ultra` (HUB 8735 ultra), `amb82-mini` and `rtl8735b-empty`.
- hwdef keywords for custom boards: `RTL8735B_I2CBUS`, `RTL8735B_SPIBUS`, `RTL8735B_SPIDEV`, `RTL8735B_RCOUT`, `RTL8735B_GPIO`, `RTL8735B_ADC`, added with `--extra-hwdef`.
- Flash layouts `realtek-ota` (default), `sdk-d1b6426` and `arduino-default`, chosen with `FLASH_LAYOUT`.
- Board applications: `env RTL8735B_APP_DIR` and `rtl8735b_app_init()`.
- Flashing after the build with `--upload`.
- The example overlay `hwdef/hub8735ultra/sensors-example.dat`: a BMI160 on SPI, and a BMP280 and GY-271 (QMC5883L) on I²C.
- Host tests that need no board and no SDK, `Tools/scripts/rtl8735b_test.sh`; on pushes to `main` and on PRs, GitHub Actions runs the same tests and the commit convention check.
- The getting started guide, the board test records BOARD_TESTS.md, and English versions of the home page, CONTRIBUTING, the HAL README and the getting started guide.

### Changed

Compared with the public commits before this version number:

- When two settings use the same pin (an enabled serial port counts), configure fails and names both sources of the conflict.
- When an overlay sets `RTL8735B_APP_DIR` but the application does not define `rtl8735b_app_init()`, linking fails.

### Removed

- The old `HAL_RTL8735B_*` pin and bus `define`s (`HAL_RTL8735B_I2C1_*`, `HAL_RTL8735B_SPI1_*`, `HAL_RTL8735B_RCOUT_*` and others); use the keywords above. Overlays that still use them build without errors, but the settings have no effect.

[Unreleased]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.2.1...HEAD
[0.2.1]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/rainwu0/ArduPilot-RTL8735B/tree/v0.1.0
