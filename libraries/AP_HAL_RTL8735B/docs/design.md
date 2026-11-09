# Design notes

[繁體中文](design.zh-TW.md)

Why the port behaves as it does, where that differs from ArduPilot on other platforms or is not obvious from the settings. The settings, limits and status themselves are in the reference documents linked from each section.

## Wi-Fi MAVLink waits for the barometer

ArduPilot sets up the MAVLink channel on SERIAL0, the console, before the vehicle code starts (`setup_console` in `AP_Vehicle::setup`, `libraries/AP_Vehicle/AP_Vehicle.cpp`). Copter then initializes the barometer, and only after that creates the MAVLink channels on the other SERIALn ports, Wi-Fi among them (`init_ardupilot` in `ArduCopter/system.cpp`).

If no barometer is found, boot stops at `Config Error: Baro: unable to initialise driver` before the Wi-Fi channel exists. Wi-Fi still joins the hotspot, but neither UDP nor TCP sends or receives MAVLink; the error itself reaches a ground station on SERIAL0. The board types have no sensors of their own, so a ground station connects over Wi-Fi only once an overlay adds at least a barometer. See [Wi-Fi](wifi.md) and [Describe your own board](custom-boards.md).

## SERIAL0 carries MAVLink, not text

SERIAL0 is the chip's LOG UART, the same port the SDK prints its console text on. When the HAL takes it over for MAVLink, during its initialization, text that the SDK and the HAL print with printf stops being sent, so that it does not land between MAVLink packets. The HAL's runtime messages (the Wi-Fi connection steps, the motor output mode and others) go to the ground station as STATUSTEXT instead. Boot messages printed before the HAL takes over still appear as text. The consequences for a board, and how to turn SERIAL0 off, are in the board READMEs, for example [UART mapping](../hwdef/hub8735ultra/README.md#uart-mapping) for the HUB 8735 ultra.

## DShot is sent through GPIO

The four PWM outputs on the HUB 8735 ultra headers, PF11–PF14, are the chip's PWM5–PWM8 (the pin table in `component/mbed/targets/hal/rtl8735b/pwmout_api.c:77-78` of SDK `d1b6426`), and that SDK has GDMA handshakes only for PWM0–PWM3 (`component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_gdma.h:69-72`). Without DMA, the PWM hardware cannot change the pulse width for every bit of a DShot frame. The CPU therefore sends DShot frames itself, by writing the set and clear registers of a GPIO port, all outputs at once (`dshot_shim.c`).

This has three consequences:

- Interrupts are masked while a frame is sent, so that no interrupt stretches a bit; the slower the DShot rate, the longer each frame masks them.
- All DShot outputs have to be on one GPIO port, because one register write sets them all.
- The bit widths are timed with the CPU cycle counter, so the port measures the CPU clock before its first DShot frame and does not send DShot if the clock is not the 500 MHz the timing assumes.

Frames go out at 800 Hz or more because ESCs need a steady stream: ChibiOS uses 800 Hz as its minimum too, and BLHeli_32 has problems below about 732 Hz. A timer resends the last frame when the main loop is slower.

A 0 bit whose falling edge comes late can be read as a 1. For Bluejay and BLHeli_S, the boundary between 0 and 1 is only about 60 ns longer than a 0 bit (worked out from their source at bird-sanctuary/bluejay `0368d114` and BLHeli_S Rev16.7 in bitdump/BLHeli `80ac909b`), so the port allows the edge to be at most 50 ns late and drops a frame when the cycle counter shows it would be later.

The figures and limits are in [Motor outputs](motor-outputs.md).

## Brushed and OneShot125 share a clock

Brushed output and OneShot125 use the same PWM clock with different sources: brushed motors use 40 MHz for their 10–32 kHz output, and OneShot125 uses the chip's internal 4 MHz oscillator for its 0.25 µs steps. Only one of the two can be in use in a given boot, and the other stops its output and reports why to the ground station as STATUSTEXT. See [Motor outputs](motor-outputs.md).

## Outputs stop when the main loop stalls

An ESC that keeps receiving the same pulse keeps the motor at that throttle. If the main loop stops (because it is stuck), the last value would be repeated without end. While armed, the HAL therefore stops every PWM output, and sends zero throttle on DShot, once the main loop has not completed a pass for more than 100 ms: ESCs lose their signal and stop, brushed motors stop, and servos go limp.

Values from before the stall, including values written but not yet sent, are not used again. When the main loop recovers, only the channels it writes again resume, with the new values, so a channel that is no longer written does not come back with an old value. See [Motor outputs](motor-outputs.md).

## Wi-Fi packs and retries UDP

MAVLink to a ground station is many small messages. Sending each in its own datagram means many datagrams, so the driver packs several whole frames into one, waiting at most 10 ms for it to fill. This cuts the number of datagrams to a fraction, at the cost of up to about 10 ms of delay per message.

The Wi-Fi driver's packet buffers are shared by all sockets, and under heavy traffic, such as a video stream, they run out for a while: a send then fails with `ENOBUFS` or `ENOMEM`. A packed datagram can hold a dozen or more messages, so instead of dropping it at once, the driver keeps it and tries again for up to 500 ms. Commands and parameter replies wait behind it during that time. See [Wi-Fi](wifi.md).

## External compasses are not probed by default

ArduPilot can look for every compass type built into the firmware on `external` I²C buses at boot (`AP_COMPASS_PROBING_ENABLED`). That probing runs before the `COMPASS` lines of the hwdef are handled, and registers a compass found at an address with the driver's default orientation (`_detect_backends` in `libraries/AP_Compass/AP_Compass.cpp`). The orientation written on a `COMPASS` line for the same address would then not take effect. The board types leave probing off, so that an overlay's `COMPASS` lines are what counts. See [Describe your own board](custom-boards.md).

## Parameter storage and power loss

Parameters are stored with ArduPilot's `AP_FlashStorage`, which alternates between two 64 KiB flash sectors. Losing power in the middle of a write loses only that last write. When a sector is full, storage moves to the other one, and the full sector has to be erased before it can be used again.

On this port that erase happens on the next boot, as 16 erases of 4 KiB. Losing power or pressing reset while it runs brings all parameters back to their defaults on the next boot. Sector changes are infrequent, so a parameter file saved from the ground station, as usual, is enough to recover.

An erase can also happen while the vehicle is running (a second sector change in the same boot, or rebuilding storage after a write failure). It starts only while disarmed, and while it runs, the pre-arm checks report `Param storage failed` and refuse a normal arm until it finishes. See [Flash layout](flash-layout.md).

## No inversion on the serial ports

In SDK `d1b6426`, the UART registers have inversion bits only for IrDA pulses (`irda_tx_inv`, `irda_rx_inv` in `rtl8735b_uart_type.h`), and there is no single-wire mode. The port reads this as no inversion and no half duplex in normal mode; receiving inverted 8N1 data on a board would disprove it. For SBUS, which is inverted, the signal goes through an external inverter. When ArduPilot asks for inversion while it tries SBUS, the HAL reports it as unsupported and keeps the port open at 8E2, so it receives what the inverter has turned back.

## I²C bus numbers follow the SDK

Each `RTL8735B_I2CBUS` line names the SDK's I2C controller (I2C0–I2C3), because the SDK decides which pins each controller can use (`component/mbed/targets/hal/rtl8735b/i2c_api.c`). The labels in a vendor's Arduino variant can differ: the HUB 8735 ultra's `variant.h` labels PE4/PE3 as I2C0 and PA1/PA0 as I2C2, the other way round from the SDK. See [hwdef settings](hwdef.md).
