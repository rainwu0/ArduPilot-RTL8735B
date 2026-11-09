# Feature status

[繁體中文](feature-status.zh-TW.md)

The status of each HAL feature. The status of each board is under *Specifications* in its README; what changed in each version is in the [CHANGELOG](../CHANGELOG.md).

## Status

| Status | Meaning |
|---|---|
| Works (Tn) | Used on a board or in flight, with correct results; the record is Tn in [BOARD_TESTS.md](../BOARD_TESTS.md) (in Chinese) |
| Needs | The code is there, one step is missing before it can be used; the step follows the colon |
| None | This repository does not have it |

With the board type `hub8735ultra` and no barometer, boot stops at `Config Error: Baro: unable to initialise driver` and Wi-Fi carries no MAVLink; with the sensors wired as in [Wiring Diagram](../hwdef/hub8735ultra/README.md#wiring-diagram) in the HUB 8735 ultra README, it does. A HUB 8735 ultra with an expansion board, on a 3.5-inch quadcopter controlled from a ground station over Wi-Fi, has flown (T5).

## Features

| Item | Status | Notes |
|---|---|---|
| Build system | Works (T1, T2) | See [Build the firmware](building.md) |
| Custom board builds | Works (T2) | `--extra-hwdef`, see [Describe your own board](custom-boards.md) |
| Scheduling and semaphores | Works (T2, T4, T5) | The Copter main loop runs at 400 Hz. After 20 ms of continuous busy-waiting the main thread yields one tick, and lower-priority tasks keep running |
| Yielding when the IMU delivers no samples at all | Needs: one try on a board | |
| SPI | Works (T2, T5) | BMI160 |
| I²C | Works (T2, T3, T5) | BMP280 and QMC5883L. The bus is cleared at boot. On one board, the first boot after flashing once stopped at `Config Error: Baro: unable to initialise driver`; after a reset it booted normally (T2) |
| I²C timeout handling | Needs: one try on a board | After a transfer times out, the controller is reinitialized and the bus cleared if needed; the bus is disabled only after 3 timeouts in a row |
| Several I²C buses at once | Needs: one try on a board | Each bus has its own task, timeouts and disabling |
| UART 8N1 | Needs: one try on a board | |
| Other UART formats | Needs: one try on a board | No/odd/even parity, 1 or 2 stop bits |
| Receive-only UART ports | Needs: one try on a board | TX pin set to `PIN_NC` |
| UART inversion, half duplex | None | See [Design notes](design.md#no-inversion-on-the-serial-ports) |
| Wi-Fi MAVLink (UDP) | Works (T2, T5) | STA mode |
| Default hotspot (no settings file) | Needs: one try on a board | SSID `ardupilot`, password `ardupilot1`, see [Wi-Fi](wifi.md#default-hotspot) |
| Wi-Fi messages while retrying to join | Needs: one try on a board | Joining is sent once, and `WiFi: join failed` again only when the error code changes or at most once every 30 s, see [Wi-Fi](wifi.md#messages) |
| Wi-Fi send retry and frame packing | Needs: one try on a board | See [Wi-Fi](wifi.md) |
| Wi-Fi MAVLink (TCP) | Needs: one try on a board | Chosen at build time, either this or UDP |
| Console MAVLink (SERIAL0) | Needs: one try on a board | LOG UART through the board's USB serial port at 115200 baud; once it takes over, SDK and HAL printf text is not sent and the HAL's runtime messages (including the Wi-Fi IP) go as STATUSTEXT. The chip's own USB is not used. See [UART Mapping](../hwdef/hub8735ultra/README.md#uart-mapping) in the HUB 8735 ultra README |
| Gamepad from a ground station (MAVLink) | Works (T5) | The ground station's gamepad reaches the board over MAVLink, not through an RC receiver (`RC_OPTIONS` 33 ignores the receiver) |
| RC input | Needs: one try with a receiver | Serial RC (AP_RCProtocol), on SERIAL4 by default; protocols without inversion (CRSF, ELRS and others) are supported; SBUS needs an external inverter (wiring in [RC Input](../hwdef/hub8735ultra/README.md#rc-input) in the HUB 8735 ultra README) |
| GPS | Needs: one try with a GPS | On SERIAL3 by default; on SERIAL1, change `SERIAL1_PROTOCOL` (wiring in [UART Mapping](../hwdef/hub8735ultra/README.md#uart-mapping) in the HUB 8735 ultra README) |
| Compass | Needs: orientation check and calibration | QMC5883L (I²C). An external compass needs a `COMPASS` line, see [Describe your own board](custom-boards.md) |
| External barometer and airspeed | Needs: one try on a board | ArduPilot's external I²C barometer probing and airspeed support are built into the board types; `BARO_PROBE_EXT` and `ARSPD_TYPE` turn them on |
| DShot300 | Works (T5) | 4-in-1 ESC running Bluejay. See [Motor outputs](motor-outputs.md) |
| DShot150, DShot600 | Needs: one try with an ESC | See [Motor outputs](motor-outputs.md) |
| ESC telemetry | Needs: one try with an ESC that has a telemetry wire | The ESC's TLM wire goes to the RX of a serial port, see [Motor outputs](motor-outputs.md) |
| PWM output: brushed | Needs: one try on a board | See [Motor outputs](motor-outputs.md) |
| PWM output: normal PWM, OneShot, OneShot125 | Needs: one try with an ESC | See [Motor outputs](motor-outputs.md) |
| Stopping outputs when the main loop stalls | Needs: one try on a board | While armed, outputs stop when the main loop stalls; when it resumes, only freshly written values go out |
| Battery voltage and current (analog input) | Needs: calibration | 12-bit, 3.3 V full scale; when the chip has ADC factory calibration parameters, readings are first corrected with the SDK's `hal_adc_calc_cali_val`. A battery voltage goes through a divider, with the ratio in `BATT_VOLT_MULT`; the current is set with `BATT_AMP_PERVLT` and `BATT_AMP_OFFSET`. Keep the pin at or below 3.3 V |
| Other analog pins | Needs: one try on a board | Only the analog pins listed in the board hwdef work; on hub8735ultra that is D2. A hwdef can list several (PF0–PF3, PA0–PA3), each sampled on its own |
| Parameters in flash | Works (T2, T5) | Kept across reboots. Losing power during the erase at the boot after a sector change brings all parameters back to defaults, see [Flash layout](flash-layout.md) |
| Blocking arming while an erase runs | Needs: one try on a board | A normal arm is refused during the erase, see [Flash layout](flash-layout.md) |
| SD card | Needs: one try with a card | See [SD card](sd-card.md) |
| Manual flashing (Windows tool) | Works (T1) | See [Flash the firmware](flashing.md) |
| Manual flashing (Linux tool) | Needs: one try on a board | See [Flash the firmware](flashing.md) |
| `--upload` flashing | Needs: one try on a board | `COMn` (Windows tool) and `/dev/tty*` (Linux tool), see [Flash the firmware](flashing.md) |
| Firmware OTA update | None | Firmware is updated by flashing, see [Flash the firmware](flashing.md) |
| Board applications | Needs: one try on a board | `env RTL8735B_APP_DIR`, see [Board applications](board-applications.md) |
| Flash layout `realtek-ota` (default) | Works (T1, T2) | See [Flash layout](flash-layout.md) |
| Flash layouts `sdk-d1b6426`, `arduino-default` | Needs: one try on a board | See [Flash layout](flash-layout.md) |

The status of each board: [HUB 8735 ultra](../hwdef/hub8735ultra/README.md#status), [AMB82-mini](../hwdef/amb82-mini/README.md#status), [rtl8735b-empty](../hwdef/rtl8735b-empty/README.md#status).
