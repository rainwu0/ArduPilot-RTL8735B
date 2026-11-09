# Describe your own board

[繁體中文](custom-boards.zh-TW.md)

How to adapt a board type to your own board, sensors and wiring with an overlay: another hwdef file, added at configure time. Every setting used here is listed in [hwdef settings](hwdef.md).

## Build with an overlay

```bash
./waf configure --board hub8735ultra --extra-hwdef /path/to/myboard.dat
./waf copter
```

- Start from the board type closest to your board ([`hub8735ultra`](../hwdef/hub8735ultra/README.md), [`amb82-mini`](../hwdef/amb82-mini/README.md) or [`rtl8735b-empty`](../hwdef/rtl8735b-empty/README.md)). The overlay holds only what differs.
- An overlay is not a board type, so `./waf list_boards` shows no new board.

## Add sensors

The board types have no sensors. Without a barometer, boot stops at `Config Error: Baro: unable to initialise driver`, and Wi-Fi carries no MAVLink either ([why](design.md#wi-fi-mavlink-waits-for-the-barometer)).

Sensors, their mounting orientation and default parameters go in the overlay, with the same syntax as hwdef on other ArduPilot platforms, for example `libraries/AP_HAL_ESP32/hwdef/esp32diy/hwdef.dat` in ArduPilot `9f648ccabc`. A working overlay for a BMI160 on SPI and a BMP280 and QMC5883L on I²C, on the HUB 8735 ultra, is [`examples/bmi160-bmp280-qmc5883l/overlay.dat`](https://github.com/rainwu0/ArduPilot-Hub8735Ultra/blob/v0.1.0/examples/bmi160-bmp280-qmc5883l/overlay.dat) in the examples repository (rainwu0/ArduPilot-Hub8735Ultra `v0.1.0`). For other sensors, copy it and change the `IMU`, `BARO` and `COMPASS` lines.

The IMU's `ROTATION_*` is found by testing, not from the silkscreen on the module: with the board level, roll and pitch are close to 0, and lifting the front of the board makes pitch positive. If the whole board is turned on the frame, the parameter `AHRS_ORIENTATION` covers it without changing the overlay.

## Change pins, buses and outputs

Each keyword line adds one entry. To replace a board type's whole group, clear it with `undef <keyword>`, then write every entry again. The `amb82-mini` board type does this for its GPIOs:

```text
undef RTL8735B_GPIO
RTL8735B_GPIO 17 PIN_D17 in
RTL8735B_GPIO 24 PIN_E6 out
RTL8735B_GPIO 23 PIN_F9 out
```

A serial port setting is replaced with `undef` followed by a new `define`. When two settings use the same pin, configure fails and names both; remove the setting that used the pin first.

## Add an I²C bus for external sensors

To keep the sensors on bus 1 and put external ones on another bus, add a bus with its own SDK number. On the HUB 8735 ultra, D3 and D4 can be SDK I2C2:

```text
RTL8735B_I2CBUS 2 2 PIN_E4 PIN_E3 400000 external
```

- Mark a bus that goes to a connector off the board `external`: ArduPilot probes for external sensors only there.
- Two devices on the same bus need different addresses (in the example wiring, bus 1 has the BMP280 at 0x76 and the QMC5883L at 0x0D). On a conflict, change the device's address pin or move one device to another bus.

## Use an external barometer or airspeed sensor

These work without changing the overlay. For an I²C barometer, set the parameter `BARO_PROBE_EXT` (the types to look for; the bits are in ArduPilot's parameter documentation) and reboot: it is looked for on `external` buses. For an airspeed sensor, set `ARSPD_TYPE` (default 0, off; on Plane too). Their status is in [Feature status](feature-status.md).

## Add an external compass

An external compass needs a `COMPASS` line in the overlay, with its orientation.

An overlay without `COMPASS` lines can instead add `define AP_COMPASS_PROBING_ENABLED 1`: at boot, every compass type built in is looked for on `external` buses, with the driver's default orientation (`ROTATION_ROLL_180` for the QMC5883L, for example), and the parameter `COMPASS_ORIENT` adjusts it. With `COMPASS` lines in the overlay, probing would register the compass with the default orientation first ([why](design.md#external-compasses-are-not-probed-by-default)).

## Receive on a serial port only, and free its TX pin

Setting a port's TX pin to `PIN_NC` makes it receive-only (for example for ESC telemetry, `SERIALn_PROTOCOL` 16); data written to it is dropped, and the TX pin is free for other uses. This does not apply to SERIAL0 (the LOG UART), which then does not open. For example, to receive ESC telemetry on D10 (PA3) and measure ESC current on D9 (PA2) of the HUB 8735 ultra:

```text
undef HAL_RTL8735B_SERIAL3_TX_PIN
define HAL_RTL8735B_SERIAL3_TX_PIN PIN_NC
RTL8735B_ADC 9 PIN_A2
```

The GPS then goes on SERIAL1.

## Turn off what the board does not have

| To turn off | Overlay lines |
|---|---|
| SERIAL0 (MAVLink on the LOG UART) | `undef HAL_RTL8735B_SERIAL0_ENABLED`, then `define HAL_RTL8735B_SERIAL0_ENABLED 0` |
| The microSD card | `undef HAL_RTL8735B_SDCARD_ENABLED` |
| ESC telemetry over a serial port | `undef HAL_SUPPORT_RCOUT_SERIAL` |

## Use TCP for Wi-Fi MAVLink

```text
define HAL_RTL8735B_WIFI_TCP_PORT 5760
```

The ground station then connects to port 5760 on the board; see [Wi-Fi](wifi.md).

## Choose a flash layout

```text
FLASH_LAYOUT sdk-d1b6426
```

After changing `FLASH_LAYOUT`, configure again. The layouts are in [Flash layout](flash-layout.md).

## Build in a board application

```text
env RTL8735B_APP_DIR <directory>
```

The interface, and how to run an application on a board without sensors, are in [Board applications](board-applications.md).
