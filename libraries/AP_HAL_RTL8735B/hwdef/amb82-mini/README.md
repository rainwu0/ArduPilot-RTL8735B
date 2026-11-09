# AMB82-mini

[繁體中文](README.zh-TW.md)

## Introduction

The AMB82-mini is an RTL8735B board with an Arduino board definition in Realtek's Ameba-AIoT/ameba-arduino-pro2 package. By that definition (`Arduino_package/hardware/variants/ameba_amb82-mini/variant.cpp` at `9de560f6af`), it has the same chip as the HUB 8735 ultra and the same peripheral pins.

The board type `amb82-mini` includes [`../rtl8735b-empty/hwdef.dat`](../rtl8735b-empty/hwdef.dat) and changes only the board name and the GPIO logical numbers, which become the AMB82-mini's D numbers.

The board's status is under [Status](#status). The facts on this page come from `hwdef.dat`, the settings it includes and that variant file; this repository has no other documents for the board.

## Specifications

- Processor: Realtek RTL8735B, the same chip as the HUB 8735 ultra (variant file above).
- Memory, power, size and on-board parts: not covered here; this repository has no vendor documents for the board.

### Status

| Item | Status |
|---|---|
| Boot, Wi-Fi MAVLink, serial ports, PWM outputs, RC input, analog pin, battery monitoring, compass and flashing on an AMB82-mini | Needs: one try on a board |
| PF12 and PF13 as PWM outputs | Needs: one try on a board |
| Wiring diagram with sensors | None. Needs: sensors added with an overlay, wired to an AMB82-mini and tried once |

The same chip on the HUB 8735 ultra: its [Status](../hub8735ultra/README.md#status). Each HAL feature: [Feature status](../../docs/feature-status.md).

## Where to Buy

This repository gives no purchase source for the AMB82-mini. Its Arduino board definition is in [Ameba-AIoT/ameba-arduino-pro2](https://github.com/Ameba-AIoT/ameba-arduino-pro2).

## Pinout

There is no pinout figure for this board. The board type uses these chip pins, the same as `rtl8735b-empty`; the AMB82-mini's labels for them are in the variant file.

| Use | Chip pins |
|---|---|
| I²C bus 1 (SDK I2C1) | PF2 (SDA), PF1 (SCL) |
| SPI bus 1 (SDK SPI1) | PF7 (MOSI), PF5 (MISO), PF6 (SCLK), PF8 (CS of `bmi160`) |
| PWM outputs 1–4 | PF14, PF11, PF12, PF13 |
| Analog pin 2 | PF0 |
| GPIOs 17, 24, 23 | PD17, PE6, PF9 |
| Serial ports | See *UART Mapping* |

## Wiring Diagram

The board type has no sensors; without a barometer, boot stops at `Config Error: Baro: unable to initialise driver` and Wi-Fi carries no MAVLink. Sensors are added with an overlay, as in [Describe your own board](../../docs/custom-boards.md). Status: see [Status](#status).

## UART Mapping

| SERIALn | Chip UART | TX | RX | Default protocol |
|---|---|---|---|---|
| SERIAL0 | UART1 (LOG UART) | PF4 | PF3 | MAVLink2, 115200 baud |
| SERIAL1 | UART2 | PD15 | PD16 | MAVLink2, 115200 baud |
| SERIAL2 | — | — | — | MAVLink2 over Wi-Fi |
| SERIAL3 | UART0 | PA2 | PA3 | GPS |
| SERIAL4 | UART3 | PE1 | PE2 | RC input |

- SERIAL5–SERIAL9 are not set up.
- How the AMB82-mini connects the LOG UART to its USB is not covered here.
- Status: see [Status](#status).

## PWM Output

| Output | Chip pin |
|---|---|
| 1 | PF14 |
| 2 | PF11 |
| 3 | PF12 |
| 4 | PF13 |

- PF12 and PF13 are also labeled `*LOG_RX` and `*LOG_TX` in the variant file.
- The output types and their limits are in [Motor outputs](../../docs/motor-outputs.md).
- Status: see [Status](#status).

## RC Input

A serial RC receiver connects to SERIAL4 (TX PE1, RX PE2); `SERIAL4_PROTOCOL` defaults to 23 (RC input). The protocols are those in the [HUB 8735 ultra README](../hub8735ultra/README.md#rc-input), the same chip. Status: see [Status](#status).

## Wi-Fi Module

The chip's Wi-Fi runs MAVLink on SERIAL2, as described in [Wi-Fi](../../docs/wifi.md). Status: see [Status](#status).

## GPIOs

| Logical number | Chip pin | Direction | Notes |
|---|---|---|---|
| 17 | PD17 | Input | D17 |
| 24 | PE6 | Output | D24, green LED |
| 23 | PF9 | Output | D23, blue LED |

These replace the GPIOs of `rtl8735b-empty` (`undef RTL8735B_GPIO` in `hwdef.dat`).

## Analog Pins

- Analog pin 2 is PF0 (ADC channel 0). Other analog inputs are added with `RTL8735B_ADC`, see [hwdef settings](../../docs/hwdef.md).
- No pin is set aside for RSSI or an analog airspeed sensor.
- Status: see [Status](#status).

## Battery Monitoring

The board type sets up no battery monitor. A battery voltage through a divider to analog pin 2 is read with `BATT_MONITOR` 3 (analog voltage), `BATT_VOLT_PIN` 2 and the divider ratio in `BATT_VOLT_MULT`. Status: see [Status](#status).

## Compass

The board type has no compass. An external I²C compass needs a `COMPASS` line in an overlay, see [Describe your own board](../../docs/custom-boards.md). Status: see [Status](#status).

## Firmware

```bash
./waf configure --board amb82-mini
./waf copter
```

- The image is `build/amb82-mini/sdk_build/flash_ntz.bin`. Setting up the tools and the SDK is in [Build the firmware](../../docs/building.md).
- The flash layout is the default `realtek-ota`, which the AMB82-mini's Arduino package also uses with OTA Mode=Enable (see [Flash layout](../../docs/flash-layout.md)).

## Loading Firmware

The level of PA5 at reset decides whether the chip enters download mode; which buttons do this on the AMB82-mini is not covered here. The flashing commands are in [Flash the firmware](../../docs/flashing.md). Status: see [Status](#status).

## Other Links

- Arduino board definition: `Arduino_package/hardware/variants/ameba_amb82-mini/variant.cpp` in [Ameba-AIoT/ameba-arduino-pro2](https://github.com/Ameba-AIoT/ameba-arduino-pro2) at `9de560f6af`.
- [Feature status](../../docs/feature-status.md): the status of each HAL feature.
