# hwdef settings

[繁體中文](hwdef.zh-TW.md)

The settings this HAL reads from a board type's `hwdef.dat` and from overlays added with `--extra-hwdef`. How to use them for a board is in [Describe your own board](custom-boards.md). Sensors (`IMU`, `BARO`, `COMPASS`) use the same syntax as hwdef on other ArduPilot platforms.

## Bus, output and pin keywords

Each line adds one entry. `undef <keyword>` clears the entries of that keyword written before it.

| Keyword | Fields | Value in `rtl8735b-empty` |
|---|---|---|
| `RTL8735B_I2CBUS` | Logical bus, SDK I2C number (0–3), SDA, SCL, clock, `internal` or `external` | `1 1 PIN_F2 PIN_F1 400000 external` |
| `RTL8735B_SPIBUS` | Logical bus, SDK SPI number (0–1), MOSI, MISO, SCLK, default clock | `1 1 PIN_F7 PIN_F5 PIN_F6 1000000` |
| `RTL8735B_SPIDEV` | Name, logical bus, CS, mode, low speed, high speed | `bmi160 1 PIN_F8 0 1000000 10000000` |
| `RTL8735B_RCOUT` | Pin; the order of the lines gives output channels 1, 2, … | `PIN_F14`, `PIN_F11`, `PIN_F12`, `PIN_F13`, one line each |
| `RTL8735B_GPIO` | Logical number, pin, `in` or `out` | `7 PIN_D17 in`, `25 PIN_E6 out`, `26 PIN_F9 out` |
| `RTL8735B_ADC` | Logical number, pin (PF0–PF3, PA0–PA3) | `2 PIN_F0` |

Limits, checked at build time:

- 1–4 I²C buses (one for each of SDK I2C0–I2C3), one SPI bus, 1–4 SPI devices, 1–12 PWM outputs, 1–8 analog inputs.
- Logical numbers: buses 0–31, GPIO 0–255, analog inputs 0–253. I²C logical numbers and SDK numbers do not repeat, and SPI device names are unique.
- When two settings use the same pin (an enabled serial port counts), the build fails and names both sources of the conflict.

Whether a pin can serve a peripheral is decided by the SDK at initialization; if it cannot, there is no output.

### I²C

- The SDK I2C number is the SDK's numbering (I2C0–I2C3), which can differ from the labels in a vendor's Arduino variant ([why](design.md)).
- The clock a driver asks the bus for has to equal the clock field, or the device cannot be obtained (`get_device_ptr` in `I2CDevice.cpp` returns `nullptr`). When the driver does not ask for one, ArduPilot asks for 400000.
- Each bus has its own bus task (named `APM_I2C` plus the SDK number), its own timeout count and its own blocking, so when one bus is disabled after repeated timeouts, the others carry on.
- ArduPilot probes for external sensors (an external barometer, for example) only on `external` buses.
- Two devices on the same bus cannot share an address; devices on different buses can.

### Analog inputs

- PF0–PF3 are ADC channels 0–3, and PA0–PA3 are external channels 4–7. Each pin is sampled on its own.
- PA2 and PA3 are also the pins of SERIAL3 (UART0) in the board types.

## Serial ports

| Setting | Meaning |
|---|---|
| `HAL_RTL8735B_SERIALn_UART_ID` | The chip UART of SERIALn |
| `HAL_RTL8735B_SERIALn_TX_PIN`, `HAL_RTL8735B_SERIALn_RX_PIN` | Its pins. `PIN_NC` as the TX pin makes the port receive-only: written data is dropped and the TX pin is free. Not for SERIAL0 (the LOG UART), which then does not open |
| `HAL_RTL8735B_SERIALn_ENABLED` | 1: on; 0: off |

A board type's value is replaced with `undef` followed by a new `define`. The values in the board types are in each board README, under UART Mapping.

## Other settings

| Setting | Effect | In the board types |
|---|---|---|
| `define HAL_RTL8735B_WIFI_TCP_PORT <port>` | Wi-Fi MAVLink as a TCP server on that port instead of UDP, see [Wi-Fi](wifi.md) | Not set: UDP |
| `HAL_RTL8735B_SDCARD_ENABLED` | microSD card, see [SD card](sd-card.md); `undef` turns it off | 1 |
| `HAL_SUPPORT_RCOUT_SERIAL` | ESC telemetry over a serial port, see [Motor outputs](motor-outputs.md); `undef` leaves it out | 1 |
| `define AP_COMPASS_PROBING_ENABLED 1` | Probe for every compass type built in on `external` buses at boot, with the driver's default orientation ([why it is off](design.md#external-compasses-are-not-probed-by-default)) | Not set |
| `FLASH_LAYOUT <name>` | Partition table, see [Flash layout](flash-layout.md) | `realtek-ota` |
| `env RTL8735B_APP_DIR <directory>` | Board application, see [Board applications](board-applications.md) | Not set |

## Removed settings

These `define`s no longer have any effect: `HAL_RTL8735B_I2C1_*`, `HAL_RTL8735B_SPI1_*`, `HAL_RTL8735B_BMI160_CS_PIN`, `HAL_RTL8735B_RCOUT_CHANNELS`, `HAL_RTL8735B_RCOUT_PIN0`–`PIN3`, `HAL_RTL8735B_GPIO_PIN7`/`PIN25`/`PIN26`, `HAL_RTL8735B_ADC_CHANNELS`, `HAL_RTL8735B_ADC_PIN0` and `HAL_RTL8735B_ADC_LOGICAL0`. An overlay that still uses them builds without errors, but those settings do nothing; the keywords above replace them.
