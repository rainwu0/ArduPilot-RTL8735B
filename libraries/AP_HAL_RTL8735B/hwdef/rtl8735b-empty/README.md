# rtl8735b-empty

[繁體中文](README.zh-TW.md)

## Introduction

`rtl8735b-empty` is the board type that holds this HAL's settings for an RTL8735B board, without a particular board's name or any sensors: buses, PWM outputs, analog input, GPIOs, serial ports, Wi-Fi and the SD card. Its pins are wired the same as the HUB 8735 ultra.

- `hub8735ultra` and `amb82-mini` include its `hwdef.dat` and change only the board name, and for `amb82-mini` the GPIO numbers.
- It is kept for existing builds that name it, and as a starting point for overlays; its image is in `build/rtl8735b-empty/`.
- Status: see [Status](#status).

## Specifications

This is a set of settings, not a board. In summary:

- One I²C bus (`external`), one SPI bus with one device (`bmi160`), four PWM outputs, one analog input, three GPIOs.
- SERIAL0, SERIAL1, SERIAL3 and SERIAL4 on chip UARTs, SERIAL2 on Wi-Fi.
- microSD card on (`HAL_RTL8735B_SDCARD_ENABLED 1`); ESC telemetry over a serial port on (`HAL_SUPPORT_RCOUT_SERIAL 1`).
- Lua scripting off (`AP_SCRIPTING_ENABLED 0`); terrain off (`AP_TERRAIN_AVAILABLE 0`); no logging backend by default (`HAL_LOGGING_BACKENDS_DEFAULT 0`); no notify LEDs (`DEFAULT_NTF_LED_TYPES Notify_LED_None`).
- The flash layout is the default `realtek-ota` ([Flash layout](../../docs/flash-layout.md)).

### Status

| Item | Status |
|---|---|
| This board type flashed as it is | Needs: one try on a board |

`hub8735ultra` has the same settings apart from the board name; its status is in its [Status](../hub8735ultra/README.md#status). Each HAL feature: [Feature status](../../docs/feature-status.md).

## Where to Buy

Not applicable: this board type describes no particular board. The boards are in the [HUB 8735 ultra](../hub8735ultra/README.md) and [AMB82-mini](../amb82-mini/README.md) READMEs.

## Pinout

There is no pinout figure: the chip pins below are labeled on the HUB 8735 ultra in its [Pinout](../hub8735ultra/README.md#pinout).

| Use | Chip pins | Setting |
|---|---|---|
| I²C bus 1 (SDK I2C1) | PF2 (SDA), PF1 (SCL) | `RTL8735B_I2CBUS 1 1 PIN_F2 PIN_F1 400000 external` |
| SPI bus 1 (SDK SPI1) | PF7 (MOSI), PF5 (MISO), PF6 (SCLK) | `RTL8735B_SPIBUS 1 1 PIN_F7 PIN_F5 PIN_F6 1000000` |
| SPI device `bmi160` | PF8 (CS) | `RTL8735B_SPIDEV bmi160 1 PIN_F8 0 1000000 10000000` |
| PWM outputs 1–4 | PF14, PF11, PF12, PF13 | `RTL8735B_RCOUT`, one line each |
| Analog pin 2 | PF0 | `RTL8735B_ADC 2 PIN_F0` |
| GPIOs 7, 25, 26 | PD17, PE6, PF9 | `RTL8735B_GPIO` |
| Serial ports | See *UART Mapping* | |

## Wiring Diagram

The board type has no sensors. Without a barometer, boot stops at `Config Error: Baro: unable to initialise driver` and Wi-Fi carries no MAVLink. Sensors are added with an overlay, see [Describe your own board](../../docs/custom-boards.md); a verified wiring on the same pins is in the HUB 8735 ultra's [Wiring Diagram](../hub8735ultra/README.md#wiring-diagram).

## UART Mapping

| SERIALn | Chip UART | TX | RX | Default protocol |
|---|---|---|---|---|
| SERIAL0 | UART1 (LOG UART) | PF4 | PF3 | MAVLink2, 115200 baud |
| SERIAL1 | UART2 | PD15 | PD16 | MAVLink2, 115200 baud |
| SERIAL2 | — | — | — | MAVLink2 over Wi-Fi |
| SERIAL3 | UART0 | PA2 | PA3 | GPS |
| SERIAL4 | UART3 | PE1 | PE2 | RC input |

SERIAL5–SERIAL9 are not set up. Each port is set with `HAL_RTL8735B_SERIALn_UART_ID`, `_TX_PIN`, `_RX_PIN` and `_ENABLED`, see [hwdef settings](../../docs/hwdef.md#serial-ports).

## PWM Output

| Output | Chip pin |
|---|---|
| 1 | PF14 |
| 2 | PF11 |
| 3 | PF12 |
| 4 | PF13 |

The output types and their limits are in [Motor outputs](../../docs/motor-outputs.md).

## RC Input

SERIAL4 defaults to RC input (`DEFAULT_SERIAL4_PROTOCOL SerialProtocol_RCIN`), with ArduPilot's serial RC protocols built in (`AP_RCPROTOCOL_ENABLED 1`). The protocols that work are listed in the HUB 8735 ultra's [RC Input](../hub8735ultra/README.md#rc-input).

## Wi-Fi Module

Wi-Fi is SERIAL2 (`HAL_RTL8735B_WIFI_SERIAL 2`), with MAVLink2 over UDP: local port 14555, and HEARTBEAT broadcasts to port 14550 until a ground station is known. See [Wi-Fi](../../docs/wifi.md).

Without a Wi-Fi settings file, the board joins the default hotspot that this board type sets: `WIFI_SSID_STATION` `"ardupilot"`, `WIFI_PWD` `"ardupilot1"` ([Default hotspot](../../docs/wifi.md#default-hotspot)).

## GPIOs

| Logical number | Chip pin | Direction |
|---|---|---|
| 7 | PD17 | Input |
| 25 | PE6 | Output |
| 26 | PF9 | Output |

## Analog Pins

- Analog pin 2 is PF0 (ADC channel 0). Other analog inputs are added with `RTL8735B_ADC`, see [hwdef settings](../../docs/hwdef.md).
- No pin is set aside for RSSI or an analog airspeed sensor. The default airspeed type is 0, off (`HAL_AIRSPEED_TYPE_DEFAULT 0`).

## Battery Monitoring

The board type sets up no battery monitor. A battery voltage through a divider to analog pin 2 is read with `BATT_MONITOR` 3 (analog voltage), `BATT_VOLT_PIN` 2 and the divider ratio in `BATT_VOLT_MULT`.

## Compass

The board type has no compass, and probing for external compasses is off. An external I²C compass needs a `COMPASS` line in an overlay, see [Describe your own board](../../docs/custom-boards.md).

## Firmware

```bash
./waf configure --board rtl8735b-empty
./waf copter
```

The image is `build/rtl8735b-empty/sdk_build/flash_ntz.bin`. Setting up the tools and the SDK is in [Build the firmware](../../docs/building.md). On a board without sensors, the example `examples/AppHost` runs with this board type, see [Board applications](../../docs/board-applications.md).

## Loading Firmware

The level of PA5 at reset decides whether the chip enters download mode; the buttons are on each board, for example the HUB 8735 ultra's [Loading Firmware](../hub8735ultra/README.md#loading-firmware). The flashing commands are in [Flash the firmware](../../docs/flashing.md).

## Other Links

- [`hwdef.dat`](hwdef.dat): every setting of this board type.
- [hwdef settings](../../docs/hwdef.md): what each `RTL8735B_*` keyword and `HAL_RTL8735B_*` setting does.
