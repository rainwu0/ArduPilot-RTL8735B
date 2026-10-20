# ArduPilot RTL8735B port

繁體中文：[README.md](README.md)

The hardware abstraction layer (HAL) for ArduPilot on the Realtek RTL8735B. **Not flight-tested. Do not use it for flight.**

Development and testing use the HUB 8735 ultra development board (vendor ideasHatch, main chip RTL8735B; vendor resources: https://github.com/ideashatch/HUB-8735).

First time here: work through [Getting started](GETTING_STARTED.en.md) once. How far each feature works today is in *Feature status*.

This repository is an unofficial port maintained by one person and has nothing to do with the ArduPilot development team. Report problems with this platform in this repository, not on ArduPilot's forum or issue tracker.

## Requirements

- Ubuntu 22.04, or WSL (Ubuntu 22.04)
- Realtek RTL8735B SDK, commit `d1b6426bb69339a609df4a3b7cd0059392999ddf` (https://github.com/ambiot/ambpro2_sdk), fetched by the script below

## Getting and building

Run these commands from the root of this repository:

```bash
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$PWD/../ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
./waf configure --board hub8735ultra
./waf copter
```

By default the SDK goes to `../ambpro2_sdk`, next to this repository. To put it somewhere else, run `Tools/scripts/rtl8735b_get_sdk.sh <SDK directory>`, set PATH with the export command the script prints at the end, and set the environment variable `AMEBAPRO2_SDK=<SDK directory>`.

The toolchain must be the Realtek ASDK that comes with the SDK (`arm-none-eabi-gcc --version` shows `Realtek ASDK`); the `arm-none-eabi-gcc` from system packages does not work. Image: `build/hub8735ultra/sdk_build/flash_ntz.bin`.

## Board types

### HUB 8735 ultra

`--board hub8735ultra` (`hwdef/hub8735ultra/hwdef.dat`). The pins follow `amebapro2_arduino/Arduino_package/hardware/variants/ameba_hub8735_ultra/variant.cpp` and `variant.h` in the vendor's ideashatch/HUB-8735 `870a7e08de7a`. The D numbers are that file's `AMB_Dn`, and GPIO logical numbers are the same as the D numbers.

| Use | Header pins |
|---|---|
| I²C bus 1 (SDK I2C1) | D0 (PF2, SDA), D1 (PF1, SCL) |
| SPI bus 1 (SDK SPI1) | D18 (PF5, MISO), D19 (PF6, SCLK), D20 (PF7, MOSI), D21 (PF8, CS of the SPI device `bmi160`) |
| PWM outputs 1–4 | D11 (PF14), D22 (PF11), D23 (PF12), D24 (PF13) |
| SERIAL1 (UART2, MAVLink2) | D8 (PD15, TX), D15 (PD16, RX) |
| SERIAL3 (UART0, GPS) | D9 (PA2, TX), D10 (PA3, RX) |
| SERIAL4 (UART3, RC input) | D6 (PE1, TX), D5 (PE2, RX) |
| Analog input, logical number 2 | D2 (PF0, ADC channel 0) |
| GPIO | 7 (D7, PD17, input), 25 (D25, PE6, green LED, output), 26 (D26, PF9, blue LED, output) |
| Reserved | D12, D13, D14, D16, D17, see below |
| Not assigned | D3 (PE4), D4 (PE3): an overlay can make them SDK I2C2 (SDA, SCL), or the CS and MOSI of SPI0 (the SCLK and MISO of SPI0 are PE1 and PE2, so disable SERIAL4 first) |

Reserved pins:

- D12 (PF15): the button on the board (`PUSH_BTN` in `variant.h`).
- D13 (PF10): not set by this board type. In our tests it drives the white fill light on the board; for details see the schematic the vendor published (`HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf` in ideashatch/HUB-8735 `870a7e08de7a`).
- D14 (PA5): marked Sensor power on in `variant.h`; its level at reset also decides whether the chip enters download mode (see *Flashing*).
- D16 (PF3), D17 (PF4): LOG UART, which is SERIAL0; see *Console MAVLink* below.

Console MAVLink (build and host tests only; not tried on a board):

- SERIAL0 is the LOG UART, connected through the on-board USB-to-UART bridge to the board's USB serial port, the same port used for flashing. `SERIAL0_PROTOCOL` defaults to 2 (MAVLink2) and `SERIAL0_BAUD` to 115, so a ground station on that COM port at 115200 baud connects without Wi-Fi.
- SDK boot messages and HAL text messages are printed on the same port and land between MAVLink packets; ground stations resynchronize, but a few packets may be lost. At 115200 the text can still be read with an ordinary serial terminal.
- Once ArduPilot starts using SERIAL0, bytes received on this port go only to MAVLink; the SDK console no longer receives commands.
- Flashing uses this port too: disconnect the ground station from it before flashing.
- To turn SERIAL0 off, write `undef HAL_RTL8735B_SERIAL0_ENABLED` in an overlay, then `define HAL_RTL8735B_SERIAL0_ENABLED 0`.
- The chip's own USB (the board's other USB-C) does not carry MAVLink: in the SDK's public source, the USB device side has only headers, no implementation.

RC receiver and GPS (build and host tests only; never tried with real devices):

- Connect an RC receiver to SERIAL4: the receiver's TX to D5 (RX), its RX to D6 (TX). `SERIAL4_PROTOCOL` already defaults to 23 (RC input); no parameter change is needed.
- Serial RC protocols that use 8N1 without inversion work, for example CRSF/ELRS (416666 baud) and the protocols at 115200 baud. SBUS needs 8E2 and inversion, and this HAL's serial ports do only 8N1 without inversion, so SBUS is not supported; while ArduPilot tries the formats in turn, the port is closed for a format it does not support and opened again for the next one.
- Connect a GPS to SERIAL3: the GPS's TX to D10 (RX), its RX to D9 (TX). `SERIAL3_PROTOCOL` already defaults to 5 (GPS), and ArduPilot finds the baud rate by itself. To use SERIAL1 (D8 TX, D15 RX) instead, set `SERIAL1_PROTOCOL` to 5; its default is MAVLink2.
- Levels: the RTL8735B's I/O is 3.3 V, and whether its pins tolerate 5 V is unknown. A receiver or GPS with 5 V signals needs a level shifter.
- Host tests cover how the serial port behaves while ArduPilot tries RC formats in turn and while the GPS driver changes baud rates (`tests/hal/uart_driver_host_test.cpp`).

Other notes:

- I²C always uses the SDK's numbering: `variant.h` labels PE4/PE3 as I2C0 and PA1/PA0 as I2C2, while the SDK's pin table has them the other way round (`component/mbed/targets/hal/rtl8735b/i2c_api.c`).
- When two settings use the same pin (an enabled serial port counts), the build fails and names both sources of the conflict. Before using D3, D4 or any other pin, remove the setting that used it.
- `rtl8735b-empty` is wired the same as `hub8735ultra` and is kept for existing builds; its image is in `build/rtl8735b-empty/`.

#### Connecting sensors

The board type itself has no sensors, and without a barometer boot stops at Config Error. Below is one way to connect an IMU, a barometer and a compass to the HUB 8735 ultra. It has been verified on a board: after boot all three sensors are detected (the first boot after flashing once showed a barometer initialization failure; see the I²C row in *Feature status*), a ground station on Wi-Fi receives heartbeats and parameters, parameters are kept across reboots, and the attitude moves the right way when the front or back end of the board is lifted (T2 in [BOARD_TESTS.md](BOARD_TESTS.md), in Chinese).

| Sensor | Module | Connect to |
|---|---|---|
| IMU | GY-BMI160 | SPI bus 1: D18 (MISO, to SAO/SDO on the module), D19 (SCLK, to SCL), D20 (MOSI, to SDA), D21 (CS) |
| Barometer | BMP280 | I²C bus 1: D0 (SDA), D1 (SCL); SDO to GND (address 0x76), CSB to 3.3 V (selects I²C mode) |
| Compass | GY-271 (QMC5883L) | I²C bus 1: shares D0 and D1 with the BMP280 |

- Connect all three modules to 3.3 V and GND: the RTL8735B's I/O is 3.3 V, and the signal levels must match. Use the 3.3 V and GND marked on the silkscreen next to the board's header pins; if unsure, check the schematic the vendor published (`HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf` in ideashatch/HUB-8735 `870a7e08de7a`).
- Pin names on the modules vary by maker; go by the silkscreen on the module you have.

```bash
./waf configure --board hub8735ultra --extra-hwdef libraries/AP_HAL_RTL8735B/hwdef/hub8735ultra/sensors-example.dat
./waf copter
```

The overlay is `hwdef/hub8735ultra/sensors-example.dat`. It is not a board type, so `./waf list_boards` shows no new board. To use other sensors, change only the three lines `IMU`, `BARO` and `COMPASS`; the syntax is the same as hwdef on other ArduPilot platforms, for example `libraries/AP_HAL_ESP32/hwdef/esp32diy/hwdef.dat` in ArduPilot `9f648ccabc`.

Mounting orientation:

- The IMU's `ROTATION_*` has to be tested; the silkscreen on the module is not enough. In our tests, a GY-BMI160 with the component side up and `ROTATION_NONE` made a level board read as upside down. The value in the overlay is what we measured on the example board: component side up, and the front is the end opposite the USB connectors.
- If you mount it differently, test again: lifting the front of the board must make pitch positive, and with the board level, roll and pitch must be close to 0.
- If the whole board is turned on the frame, leave the overlay alone and change the parameter `AHRS_ORIENTATION`. For the orientation of an external compass, use the parameter `COMPASS_ORIENT`.

### AMB82-mini

`--board amb82-mini` (`hwdef/amb82-mini/hwdef.dat`): the AMB82-mini has the same chip as the HUB 8735 ultra and the same peripheral pins (per `Arduino_package/hardware/variants/ameba_amb82-mini/variant.cpp` in Realtek Ameba-AIoT/ameba-arduino-pro2 9de560f6af). This board type only changes the GPIO logical numbers to the AMB82-mini's D numbers: 17 (PD17, input), 24 (PE6, green LED), 23 (PF9, blue LED). The Flash layout is the default `realtek-ota` here too.

- Status: build only, not tried on a board.
- Of the four PWM outputs PF11–PF14, PF12 and PF13 are also labeled `*LOG_RX` and `*LOG_TX` in that file; whether they work as PWM outputs is not confirmed.

### Custom boards

Other RTL8735B boards are described with `--extra-hwdef`: serial ports with `undef` followed by a new `define`; buses, outputs and pins use the keywords below: each line adds one entry; to replace a whole group, first `undef <keyword>` to clear it, then write it again.

| Keyword | Fields | Value in `hub8735ultra` |
|---|---|---|
| `RTL8735B_I2CBUS` | logical bus, SDK I2C number (0–3), SDA, SCL, clock, `internal` or `external` | `1 1 PIN_F2 PIN_F1 400000 external` |
| `RTL8735B_SPIBUS` | logical bus, SDK SPI number (0–1), MOSI, MISO, SCLK, default clock | `1 1 PIN_F7 PIN_F5 PIN_F6 1000000` |
| `RTL8735B_SPIDEV` | name, logical bus, CS, mode, low speed, high speed | `bmi160 1 PIN_F8 0 1000000 10000000` |
| `RTL8735B_RCOUT` | pin; the order of the lines gives output channels 1, 2, … | `PIN_F14`, `PIN_F11`, `PIN_F12`, `PIN_F13`, one line each |
| `RTL8735B_GPIO` | logical number, pin, `in` or `out` | `7 PIN_D17 in`, `25 PIN_E6 out`, `26 PIN_F9 out` |
| `RTL8735B_ADC` | logical number, pin (PF0–PF3, PA0–PA3) | `2 PIN_F0` |

1–4 I²C buses (one for each of SDK I2C0–I2C3), one SPI bus, 1–4 SPI devices, 1–12 PWM outputs, 1–8 analog inputs. Mistakes are reported at build time. Ranges of logical numbers: buses 0–31, GPIO 0–255, analog inputs 0–253; I²C logical numbers and SDK numbers must not repeat, and SPI device names must be unique. Whether a pin can serve that peripheral is decided by the SDK at initialization; if it cannot, there is no output.

- The clock in `RTL8735B_I2CBUS`: the clock a driver asks the bus for must equal this field, or the device cannot be obtained (`get_device_ptr` in `I2CDevice.cpp` returns `nullptr`). When the driver does not ask for one, ArduPilot asks for 400000.
- Several I²C buses: each has its own bus task (named `APM_I2C` plus the SDK number), its own timeout count and its own blocking, so when one bus is disabled after repeated timeouts, the others carry on. For example, to keep the on-board sensors on bus 1 and put external ones on D3 and D4, add `RTL8735B_I2CBUS 2 2 PIN_E4 PIN_E3 400000 external` to the overlay.
- `internal` and `external`: ArduPilot probes for external sensors (an external compass, for example) only on `external` buses, so mark a bus that goes to a connector off the board as `external`.
- Analog inputs: PF0–PF3 are ADC channels 0–3 and PA0–PA3 are external channels 4–7; each pin is sampled on its own. PA2 and PA3 are also the pins of SERIAL3 (UART0): to use PA2 as an analog input, first make SERIAL3 receive-only (next item) or turn SERIAL3 off.
- Receive-only serial ports: `define HAL_RTL8735B_SERIALn_TX_PIN PIN_NC` (after `undef`). The port only receives (for example ESC telemetry, `SERIALn_PROTOCOL` 16) and written data is dropped; the TX pin is free for other uses. For example, to receive ESC telemetry on D10 (PA3) and measure ESC current on D9 (PA2), the overlay has `undef HAL_RTL8735B_SERIAL3_TX_PIN`, `define HAL_RTL8735B_SERIAL3_TX_PIN PIN_NC` and `RTL8735B_ADC 9 PIN_A2`. The GPS then goes on SERIAL1.
- Address conflicts: two devices on the same bus cannot share an address (bus 1 on the board already has the BMP280 at 0x76 and the QMC5883L at 0x0D); devices on different buses can. On a conflict, change the device's address pin or move one of them to another bus.
- The old syntax has been removed: the `define`s `HAL_RTL8735B_I2C1_*`, `HAL_RTL8735B_SPI1_*`, `HAL_RTL8735B_BMI160_CS_PIN`, `HAL_RTL8735B_RCOUT_CHANNELS`, `HAL_RTL8735B_RCOUT_PIN0`–`PIN3`, `HAL_RTL8735B_GPIO_PIN7`/`PIN25`/`PIN26`, `HAL_RTL8735B_ADC_CHANNELS`, `HAL_RTL8735B_ADC_PIN0` and `HAL_RTL8735B_ADC_LOGICAL0` no longer have any effect. If an overlay still uses them, the build does not fail; the settings just stop working without a word. Change them to the keywords in the table above.

Board types have no sensors. Without a barometer, boot stops at `Config Error: Baro: unable to initialise driver`, and Wi-Fi then carries no MAVLink either (see *Wi-Fi*). Write the sensors, mounting orientation and default parameters in another hwdef file and add it with `--extra-hwdef`:

```bash
./waf configure --board hub8735ultra --extra-hwdef /path/to/myboard.dat
```

### Board applications

A custom board can build SDK-side C code into the same firmware. Add one line to the overlay hwdef:

```
env RTL8735B_APP_DIR <directory>
```

- A relative path is taken from the root of this repository; if the directory does not exist, configure fails.
- The `.c` files in the directory are compiled together with the SDK sources, with the SDK's includes and macros; if the directory has no `.c` file, the build fails.
- The application uses only the SDK and `app/rtl8735b_app.h`, not ArduPilot's internals. Keep task priorities at 3 or lower: the ArduPilot main thread is 4, see `Scheduler.h`.
- Without this line, the firmware contains no application.

The firmware calls the application's functions at the times below; the declarations and full descriptions are in `app/rtl8735b_app.h`. `rtl8735b_app_init` must be defined: if it is not, linking fails (`undefined reference to rtl8735b_app_init`); it never silently does nothing. The other two are optional; when they are not defined, the firmware uses empty functions.

| Function | When it is called | Restrictions |
|---|---|---|
| `rtl8735b_app_init` | Once per boot: after the ArduPilot task is created and before the scheduler starts; no ArduPilot code has run yet | Only create tasks; `printf` and direct Flash access through the SDK are fine; do not wait for anything that needs the scheduler |
| `rtl8735b_app_after_setup` | Once per boot: in the ArduPilot main task, after `setup()` finishes and before the first main loop | Only create tasks or set variables; do not block, or the main loop starts late |
| `rtl8735b_app_before_reset` | Right before the firmware resets itself (`Scheduler::reboot()`, for example a reboot command from the ground station or a parameter that needs a reboot); the outputs are already safe, and the reset follows when it returns | May be called from any task: do not block, take locks, or print |

`rtl8735b_app_before_reset` is for GPIOs that double as boot-mode pins. The level of PA5 at reset decides whether the chip enters download mode (see *Flashing*). If the application drives PA5 as an output (D14 on the HUB 8735 ultra, marked Sensor power on in `variant.h`), it must put the pin back to its boot-time state here; otherwise the chip may stay in download mode after the firmware reboots and not start. This covers only reboots done by the firmware itself: watchdog and crash resets do not come through here, and an application that calls the SDK's `sys_reset()` itself must handle these pins first.

The application can call these functions:

| Function | Returns or does | Restrictions |
|---|---|---|
| `rtl8735b_ardupilot_armed()` | 1: armed (`AP_HAL::Util::get_soft_armed()`, motor tests included); 0: disarmed | Reads variables only; does not block or take locks; any task or FreeRTOS timer callback may call it |
| `rtl8735b_ardupilot_system_initialized()` | 1: `setup()` has finished; 0: not yet | Same as above |
| `rtl8735b_ardupilot_ms_since_main_loop()` | Milliseconds since the main loop last finished, computed at the time of the call, so it keeps growing while the main loop is stuck; `UINT32_MAX` before the first loop finishes | Same as above |
| `rtl8735b_ardupilot_outputs_safe()` | Stops all PWM outputs and puts the safety switch back to safe (`RCOutput::force_safety_on()`), for example before the application resets with `sys_reset()`; check that the vehicle is disarmed before resetting | Waits for the main task to release the output lock: any FreeRTOS task may call it, but not an interrupt or a FreeRTOS timer callback |

The application's `.c` files also get these compile definitions, with values from the hwdef's `FLASH_LAYOUT` (see *Flash layout*): `RTL8735B_FLASH_LAYOUT_NAME` (the layout name as a string, for example `"realtek-ota"`), and `RTL8735B_FLASH_FW1_ADDR`, `RTL8735B_FLASH_FW1_LEN`, `RTL8735B_FLASH_FW2_ADDR`, `RTL8735B_FLASH_FW2_LEN` (the address and length in bytes of fw1 and fw2 in this image's partition table). The firmware's own sources do not get them.

On a board without sensors, run the application with the `examples/AppHost` example: it starts only Wi-Fi and the main loop, does not initialize sensors or arm, and leaves the PWM outputs safe, so `setup()` finishes and the main loop runs. A normal vehicle image on a board without an IMU stops at inertial sensor initialization, and `rtl8735b_app_after_setup` is never called.

```bash
./waf configure --board rtl8735b-empty --extra-hwdef /path/to/myapp.dat
./waf --targets examples/AppHost
```

### Flash layout

The partition table in `flash_ntz.bin` (addresses and lengths of the firmware slots, ISP IQ, NN model area and so on) is set by `FLASH_LAYOUT` in hwdef, added with `--extra-hwdef`:

```
FLASH_LAYOUT sdk-d1b6426
```

| Layout | fw1 | fw2 | Image limit | Source |
|---|---|---|---|---|
| `realtek-ota` (default) | 0x060000, 4 MiB | 0x520000, 4 MiB | 4,194,304 bytes | `amebapro2_partitiontable_OTA.json` of the Realtek Arduino package with OTA Mode=Enable (Ameba-AIoT/ameba-arduino-pro2 93d6351), the same as the example partitions in Ameba-AIoT/ameba-rtos-pro2 eb5c090 |
| `sdk-d1b6426` | 0x080000, 3.5 MiB | 0x4C0000, 3.5 MiB | 3,670,016 bytes | `amebapro2_partitiontable.json` of the example project in ambpro2_sdk d1b6426 |
| `arduino-default` | 0x060000, 4 MiB | 0x520000, 64 KiB | 4,194,304 bytes | `amebapro2_partitiontable.json` of the same Arduino package with OTA Mode=Disable |

- Status: the default `realtek-ota` has booted on a HUB 8735 ultra with an image of this version (board type `hub8735ultra`); `sdk-d1b6426` and `arduino-default` are build only.
- The default is `realtek-ota`: when you flash a `flash_ntz.bin` from a default build by hand, the board's partition table changes to this layout too. The Arduino packages for the HUB 8735 ultra (vendor package 4.1.1, released in ideashatch/HUB-8735 870a7e0) and the AMB82-mini (Ameba-AIoT/ameba-arduino-pro2 93d6351) use this layout with OTA Mode=Enable, and so does the newer SDK (Ameba-AIoT/ameba-rtos-pro2 eb5c090). To keep the original partitions of SDK `d1b6426`, choose `sdk-d1b6426`.
- The build starts from the SDK's partition table and rewrites only the address and length of each record.
- The image limit is the length of fw1, because the firmware is written to fw1. If `firmware_ntz.bin` exceeds the limit, or the slot records in the built `flash_ntz.bin` differ from the layout, the build fails.
- This repository does not provide Wi-Fi firmware updates and does not write fw2; fw2 is only a record in the partition table.
- The layout is read at `./waf configure`: after changing `FLASH_LAYOUT` in hwdef or an overlay, configure again.
- Parameters are stored at 0xF80000–0xFA0000, the same in all three layouts. The NOR file system area (0xF64000–0x1000000) of the Arduino package (93d6351) and the newer SDK (eb5c090) covers this range; it only conflicts when the Flash file system is enabled, and this port does not enable it.
- Parameters are stored with ArduPilot's `AP_FlashStorage`, alternating between two 64 KiB sectors. Losing power in the middle of a write loses only that last write. One exception: after one sector fills and storage moves to the other, the next boot erases the full sector (16 erases of 4 KiB; the time has not been measured). Losing power or pressing reset during that time brings all parameters back to their defaults on the next boot; the pre-arm checks show the missing calibration. Sector changes are infrequent, but keep saving parameters to a file with the ground station as usual, and load them back when needed.

## Motor outputs

| `MOT_PWM_TYPE` | Output |
|---|---|
| 0 Normal, 8 PWMRange, 9 PWMAngle | `RC_SPEED` 50–490 Hz, pulse width 400–2100 µs |
| 1 OneShot | the same continuous PWM as above (the chip has no one-shot trigger) |
| 2 OneShot125 | pulse width 125–250 µs (1000–2000 divided by 8) in 0.25 µs steps; fixed 1 kHz continuous output, `RC_SPEED` is ignored |
| 3 Brushed | `RC_SPEED` 10–32 kHz |
| 4 DShot150, 5 DShot300, 6 DShot600 | Frames sent through GPIO, all outputs at once; see below |

- Brushed and OneShot125 share the PWM clock (brushed uses 40 MHz, OneShot125 the chip's internal 4 MHz oscillator), so only one of them works in a given boot; the other stops its output and prints the reason on the console.
- While armed, if the main loop has not completed a pass for more than 100 ms (for example because it is stuck), all PWM outputs stop (normal PWM sends no more pulses, brushed motors get duty 0), so ESCs lose their signal and stop and brushed motors stop instead of the last value being output indefinitely; servos go limp as well. Values from before the stall, including values written but not yet sent, are not used again: after the main loop recovers, only channels the main program writes again resume, with the new values, and an empty output does not bring old values back.
- Not supported: DShot1200 (7) and LED outputs (NeoPixel, ProfiLED).
- DShot: the four outputs PF11–PF14 on the HUB 8735 ultra are PWM5–PWM8 (the pin table in `component/mbed/targets/hal/rtl8735b/pwmout_api.c:77-78`), and SDK d1b6426 only has GDMA handshakes for PWM0–PWM3 (`component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_gdma.h:69-72`), so the PWM hardware cannot change the pulse width every bit. DShot frames are therefore sent by the CPU writing the GPIO port's set and clear registers, on all outputs at once (`dshot_shim.c`).
  - Interrupts are masked while each frame is sent: about 107 µs at DShot150, 53 µs at DShot300 and 27 µs at DShot600. DShot300 is recommended.
  - Each output from the main program sends one frame; when the main loop has not output for more than 4 ms, a timer resends the last frame. When the main loop has not completed a pass for more than 100 ms (for example because it is stuck), the resent throttle becomes 0 and the motors stop instead of running on at the last throttle, and DShot commands queued before the stall are not sent. After the main loop recovers, only channels the main program writes again are sent, with the new values.
  - The first switch to DShot measures the CPU clock with the CPU cycle counter; if the counter is missing or the clock is not 500 MHz, DShot is not sent and the reason is printed on the console.
  - If the cycle counter stalls, goes backwards or jumps too far in the middle of a frame (for example when an interrupt that cannot be masked holds up the loop), the bit widths would come out wrong: all pins are pulled low and that frame is dropped, and the next output is sent as usual. The first time this happens, a warning goes to the console and the ground station.
  - All DShot outputs have to be on the same GPIO port; the PWM pins on the headers are all on port F.
  - Once an output switches to DShot, its pin stays GPIO for the rest of the boot. Changing `MOT_PWM_TYPE` needs a reboot.
  - The `RCOut:` line in the boot messages shows each output's mode, for example `RCOut: DS300:1-4`.
  - DShot commands sent through `send_dshot_command` (for example beeps from the ESCs) and the BLHeli_S bit widths (`SERVO_DSHOT_ESC`) are supported.
  - Not supported: bidirectional DShot (RPM readback), 3D and reversed channel settings (`SERVO_BLH_3DMASK`, `SERVO_BLH_RVMASK`) and flashing ESC firmware through the flight controller (BLHeli passthrough).
- ESC telemetry (TLM wire): connect the telemetry wire of BLHeli_32, AM32 and similar ESCs to the RX of a serial port (only RX is needed) and set that port's `SERIALn_PROTOCOL` to 16; the baud rate is set to 115200 automatically. `MOT_PWM_TYPE` has to be DShot, and the motor pole count goes in `SERVO_BLH_POLES`. The ESCs' current and voltage can serve as a battery monitor with `BATTn_MONITOR` 9 and are also in the `ESC` log and the MAVLink `ESC_TELEMETRY` messages. A custom board that does not need it can add the line `undef HAL_SUPPORT_RCOUT_SERIAL` to its overlay.
- DShot and ESC telemetry have not been tried on a board. Not yet confirmed before that: the cycle counter exists and the CPU runs at 500 MHz; edge jitter is within what ESCs accept; masking interrupts for each frame does not disturb Wi-Fi or the main loop.
- OneShot and OneShot125 have not been tried on a board. Not yet confirmed for OneShot125:
  - The clock comes from the chip's internal RC oscillator; its accuracy and temperature drift have not been measured, nor whether the clock is usable right after switching to 4 MHz.
  - The register writes for period and pulse width are inferred from the brushed code in Realtek's public sources (`component/soc/8735b/fwlib/rtl8735b/source/ram/hal_pwm.c:333-380` in Ameba-AIoT/ameba-rtos-pro2 eb5c090); the actual pulse width has not been measured.
  - That the output stays low while the pulse width is 0.
  - Whether ESCs accept a continuous 1 kHz OneShot125 signal.

  After fitting ESCs, calibrate them first.

## Wi-Fi

At build time, point the environment variable `RTL8735B_WIFI_CONFIG` at a local settings file (two lines, `ssid=` and `password=`); without it, Wi-Fi is not enabled. The credentials are written only into the build directory.

MAVLink uses UDP by default: local port 14555, and while there is no ground station yet, the HEARTBEAT is broadcast to port 14550 on the subnet. To use a TCP server instead, add one line with `--extra-hwdef`:

```
define HAL_RTL8735B_WIFI_TCP_PORT 5760
```

TCP serves one ground station at a time; the ground station connects to port 5760 on the board.

How UDP sends to the ground station:

- Several whole MAVLink frames are packed into one datagram (up to 1,472 bytes), which cuts the number of datagrams to a fraction. When a datagram is not full, the driver waits at most 10 ms, so each message goes out at most about 10 ms later (`WiFiCoalesce.h`). HEARTBEAT broadcasts sent before a ground station is known still go one frame per datagram; TCP is not affected.
- When a send fails because the Wi-Fi driver is out of packet buffers (`ENOBUFS`, `ENOMEM`), that datagram is kept and sent again on the next step (about 1 ms later), for up to 500 ms from the first failure, and only then dropped; other send failures are dropped at once (`WiFiTxRetry.h`). The driver's packet buffers are shared by all sockets and run out for a while under heavy traffic such as a video stream.
- A retry can hold the link for up to 500 ms, and commands and parameter replies queue behind it during that time; the 16 KiB send buffer holds that much MAVLink. If the datagram still cannot be sent after 500 ms, the whole datagram is dropped, which can be a dozen or more messages.

Copter initializes the barometer first and only then creates the MAVLink channels on SERIALn, Wi-Fi among them (`init_ardupilot` in `ArduCopter/system.cpp`). If no barometer is found, boot stops at Config Error: Wi-Fi still joins the hotspot, but neither UDP nor TCP sends or receives any MAVLink.

Security limits:

- The credentials are compiled into the firmware as a constant array; anyone who has the image file may be able to read them. Do not share images that contain credentials, and do not attach them when you report a problem.
- MAVLink over Wi-Fi (UDP) has no authentication at all. While there is no ground station yet, or after the ground station has sent nothing for more than 3 seconds, any source that sends packets to the local port (including other flight controllers or programs on the same subnet) is taken as the new ground station, and its RC override is accepted too. With no ground station, the HEARTBEAT is broadcast to the whole subnet. Use it only on a dedicated network you control.
- TCP has no authentication either. Once the ground station has sent nothing for more than 3 seconds, a new connection from any host on the subnet replaces it.

## SD card

The board's microSD card is read and written through FatFs (`libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp`), and the ground station's MAVLink FTP sees the files on the card.

- The `rtl8735b-empty` board type turns it on by default (`define HAL_RTL8735B_SDCARD_ENABLED 1` in the hwdef), and so do `hub8735ultra` and `amb82-mini`, which include it. For a custom board without an SD card slot, add the line `undef HAL_RTL8735B_SDCARD_ENABLED` to the overlay.
- Format the card as FAT32 or exFAT on a computer first. A card without a filesystem can also be formatted from the ground station with the MAVLink command `MAV_CMD_STORAGE_FORMAT`, which erases the whole card.
- The card is mounted the first time a file is accessed after boot. Without a card, a mount is retried at most once every 3 seconds; after a card is inserted, the next access mounts it.
- Logging: filesystem logging is built into the firmware but not written by default (`LOG_BACKEND_TYPE` defaults to 0). To write logs to the card, set `LOG_BACKEND_TYPE` to 1; logs go to `/APM/LOGS`. With that set and no card inserted, the pre-arm checks report `Logging failed` or `No SD card`.
- The SD signals stay at 3.3 V; the 1.8 V high-speed modes are not used.

FatFs is in `fatfs/`: R0.14b, taken from ArduPilot's ChibiOS submodule (`ext/fatfs` at commit `9aebaf4a40`, which `modules/ChibiOS` points to), with the same content as ChaN's R0.14b release. `fatfs/ffconf.h` is this platform's configuration, taken from ArduPilot's `libraries/AP_HAL_ChibiOS/hwdef/common/ffconf.h`.

## Flashing

### `--upload`

Add `--upload` and `--upload-port` to the build, and when the build finishes it flashes `flash_ntz.bin` with the SDK's `uartfwburn` (`tools/Pro2_PG_tool _v1.3.0.zip`):

```bash
./waf copter --upload --upload-port COM7          # WSL: runs the Windows tool through interop
./waf copter --upload --upload-port /dev/ttyUSB0  # Linux: runs the Linux tool
```

- When the build prints `請讓開發板進入燒錄模式` ("put the board into download mode"), use the board's buttons to put the chip into download mode (decided by the level of PA5 at reset; on the HUB 8735 ultra, hold the upgrade button, press reset briefly, then release upgrade). If the chip is not in download mode within 60 seconds, it retries automatically; on timeout nothing is written. After `download success`, reset once to boot.
- `COMn` needs WSL interop (running Windows programs from WSL); without it, `--upload` tells you before compiling starts. On WSL with systemd, after the distribution shuts down when idle and starts again in the same WSL VM, interop may not register again: run `wsl --shutdown` on Windows to restart WSL, and keep a WSL terminal open while building and flashing.
- `/dev/ttyXXX` needs read and write access to the device (the `dialout` group); on WSL, first attach the USB serial device to WSL with usbipd.
- The environment variable `AP_OVERRIDE_UPLOAD_CMD` replaces the command with your own: it is run with the image path appended, followed by `--port <port>` when `--upload-port` is given.
- It only downloads, without erasing, and it does not reset the board afterwards.

`--upload` has not been verified on a board yet, see *Feature status*; the manual flashing below has been used on Windows.

### Manual flashing

Flashing is done on the Windows side (WSL2 does not see USB serial ports by default):

1. Unpack the SDK's `tools/Pro2_PG_tool _v1.3.0.zip` (the file name contains a space); it contains `uartfwburn.exe`.
2. Connect the board's USB serial port to the computer and look up the COM port number in Device Manager.
3. Only after the image is built, put the chip into download mode (decided by the level of PA5 at reset; use the board's buttons). Flash right after entering download mode; if you wait too long, `ping fail` or `ucfg fail` appears, and entering download mode again fixes it.
4. Run:

   ```powershell
   .\uartfwburn.exe -p COM7 -f <path>\flash_ntz.bin -b 2000000 -U
   ```

   Change `-p` to the actual port, and give `-f` an absolute path. After `download success`, reset once to boot.

Do not unplug USB while flashing. Close other programs that open serial ports by themselves (for example ground station software) first, or switch them to a network connection. The SDK also includes a Linux version, `uartfwburn.linux`, which has not been tried.

## Changes to ArduPilot shared code

- `ArduCopter/takeoff_check.cpp`: in builds without ESC telemetry (`HAL_WITH_ESC_TELEM` is 0, for example a custom board whose overlay has `undef HAL_SUPPORT_RCOUT_SERIAL`), the original pre-takeoff check is not compiled at all, and after arming the motors stay at ground idle. Changed so that without ESC telemetry only the motor RPM check is skipped and the CPU load check still runs (this HAL does not report system load yet, so for now it does not block); builds with ESC telemetry (the board types' default) are not affected and use the original upstream check. This repository takes the commit of upstream PR 34070 (https://github.com/ArduPilot/ardupilot/pull/34070, by yuiseki) as it is; as of 2026-10-07 the PR has not been merged upstream.
- `libraries/AP_HAL/AP_HAL_Boards.h`: `HAL_BOARD_RTL8735B` is 100, clear of the platform numbers upstream assigns in sequence.
- `Tools/ardupilotwaf/boards.py` (waf board registration) and `libraries/AP_Filesystem/AP_Filesystem.h` (this platform's newlib has no `dirent.h`, so the definitions are added here): add this platform without changing the behavior of other platforms.
- `libraries/AP_Filesystem/AP_Filesystem.cpp` and `AP_Filesystem_config.h`: add this platform's SD card backend `AP_Filesystem_RTL8735B` (a new file) without changing the behavior of other platforms.

## Feature status

"Verified on a board": has run on a HUB 8735 ultra, with a record in [BOARD_TESTS.md](BOARD_TESTS.md) (in Chinese); the record number is in parentheses. "To be rechecked": there were earlier board results, but they cannot be confirmed to come from the board under test, or there is no record for the current version, so it has to be verified again. With only the board type `hub8735ultra` and no sensors, the board boots and joins Wi-Fi but stops at the barometer Config Error, so there is no Wi-Fi MAVLink (T1); wired as in *Connecting sensors*, Wi-Fi MAVLink works (T2; the barometer occasionally fails to initialize, see the I²C row).

| Item | Status | Notes |
|---|---|---|
| Build system | Verified on a board (T1, T2) | |
| Scheduling and semaphores | Verified on a board (T2, T4) | The Copter main loop runs on the board (heartbeat, parameters, attitude updates); in normal running it measured 399.9 Hz over 120 seconds, with 0.07% long loops (T4). The main thread yields one tick after 20 ms of continuous busy-waiting: with the IMU's SCLK pulled, the main loop and lower-priority tasks kept running, but whether the yield triggered was not recorded; an IMU that delivers no samples at all has not been tried on a board (T4) |
| SPI | Verified on a board (T2) | BMI160 |
| I²C | Verified on a board (T2, T3) | BMP280 and QMC5883L share bus 1. The bus is cleared at boot: over 23 warm reboots, the first boot after writing the image and a cold boot, the barometer always initialized, and the occasional failure at boot recorded in T2 did not appear again (T3); the T3 image had the code from before each bus got its own task, and later versions have not been tried on a board. Timeout handling not tried on a board: after a transfer times out, the controller is reinitialized and the bus cleared if needed; the bus is disabled only after 3 timeouts in a row. Several buses at once (each with its own timeouts and disabling): build and host tests only, not tried on a board |
| UART | To be rechecked | 8N1 only. Receive-only ports (TX set to `PIN_NC`): build and host tests only, not tried on a board |
| Wi-Fi MAVLink (UDP) | Verified on a board (T2) | STA mode. Send retry and frame packing (see *Wi-Fi*): designed and implemented; verified by host tests only — not yet tested with a real device |
| Wi-Fi MAVLink (TCP) | Build only | Chosen at build time, either this or UDP |
| Console MAVLink (SERIAL0) | Build only | LOG UART through the board's USB serial port at 115200 baud; SDK and HAL text messages land between packets. The chip's own USB is not used. See *Console MAVLink* under *Board types* |
| RC input | Build only | Serial RC (AP_RCProtocol), on SERIAL4 by default; protocols with 8N1 and no inversion (CRSF, ELRS and others) work, SBUS does not (wiring in *Board types*). Host tests cover trying the formats in turn; never tried with a receiver. RC override from a ground station goes over MAVLink, not through RC input; its use on a board is to be rechecked |
| GPS | Build only | On SERIAL3 by default; on SERIAL1, change `SERIAL1_PROTOCOL` (wiring in *Board types*). Host tests cover the automatic baud rate changes; never tried with a GPS |
| PWM output | To be rechecked | Brushed motor PWM has driven motors before; normal PWM for ESCs, OneShot and OneShot125 not tried on a board, see *Motor outputs* |
| DShot and ESC telemetry | Build only | Designed and implemented; verified by host tests only — not yet tested with a real device. Host tests cover the DShot150/300/600 frames and timing, the GPIO writes that send them, the telemetry request bit and DShot commands; ESC telemetry over the TLM wire is build only. See *Motor outputs* |
| AnalogIn | Build only | 12-bit, converted to volts linearly with a 3.3 V full scale; accuracy and zero offset not measured on a board (the zero offset can be corrected with parameters such as `BATT_VLT_OFFSET`). Only the analog pins listed in the board hwdef work; on hub8735ultra that is D2. A hwdef can list several (PF0–PF3, PA0–PA3), each sampled on its own; several at once is build and host tests only, and whether PA0–PA3 convert the same way as PF0–PF3 has not been checked on a board. How much voltage a pin tolerates is not known, so keep the pin at or below 3.3 V. A battery voltage has to go through a divider first, with the ratio set in `BATT_VOLT_MULT` |
| Parameters in Flash | Verified on a board (T2) | Kept across reboots; kept across power loss not verified. Losing power during the erase at the boot after a sector change brings all parameters back to defaults, see *Flash layout* |
| SD card | Build only | Designed and implemented; verified by host tests only — not yet tested with a real device. Host tests with a fake card in RAM cover mounting and retries, formatting, file and directory operations, remounting after a write error, and a pulled card; for usage see *SD card* |
| `--upload` flashing | Build only | Neither `COMn` (Windows tool) nor `/dev/tty*` (Linux tool) tried on a board; for manual flashing see *Flashing* |
| Firmware OTA update | Not provided | This repository does not provide Wi-Fi firmware updates; update firmware by flashing, see *Flashing* |
| Custom board builds | Build only | `--extra-hwdef` |
| Board applications | Build and host tests only, not tried on a board | `env RTL8735B_APP_DIR`, see *Board applications*. Host tests cover the reboot order (outputs made safe before the application is called), the empty defaults of the optional functions, and the main-loop time |
| Flash layout | Default layout verified on a board (T1, T2) | `FLASH_LAYOUT`; the default `realtek-ota` has booted on a board; `sdk-d1b6426` and `arduino-default` are build only, see *Flash layout* |

## License

This repository as a whole is distributed under GPL-3.0: most ArduPilot and port files are GPL-3.0-or-later, and some upstream files have other compatible licenses, as stated in each file's header. The Realtek SDK has its own license terms (`Realtek_Disclaimer-2019.pdf` in the SDK root); it is not in this repository and has to be obtained separately. The built firmware contains Realtek's precompiled libraries; this repository does not provide firmware images, and neither do its Releases or CI artifacts. Before distributing firmware you built yourself, check for yourself that it complies with both GPL-3.0 and Realtek's terms.

Several parts of this port are adapted from ArduPilot's ESP32 port (`libraries/AP_HAL_ESP32` and `Tools/ardupilotwaf/esp32.py` in ArduPilot 9f648ccabc): `Semaphores.cpp`, `Semaphores.h`, `system.cpp`, `hwdef/scripts/rtl8735b_hwdef.py`, and the hwdef generation and cmake setup in `Tools/ardupilotwaf/rtl8735b.py`; each file's header says so. How the Scheduler is split into FreeRTOS tasks also follows the ESP32 port's design. Both are GPL-3.0-or-later.

The SD card filesystem backend `libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp` is adapted from `AP_Filesystem_FATFS.cpp` in ArduPilot 9f648ccabc, as its header says. FatFs in `fatfs/` is under ChaN's FatFs license (BSD-style; see `fatfs/LICENSE.txt` and each file's header) and can be distributed together with GPL-3.0 code.
