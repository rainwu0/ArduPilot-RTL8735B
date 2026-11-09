# Motor outputs

[繁體中文](motor-outputs.zh-TW.md)

The output types the HAL supports on its PWM outputs and their limits. The status of each is in [Feature status](feature-status.md). Which pins are outputs is in each board README, for example [PWM Output](../hwdef/hub8735ultra/README.md#pwm-output) for the HUB 8735 ultra. Why DShot is sent through GPIO, and why outputs stop when the main loop stalls, is in [Design notes](design.md).

## Output types

| `MOT_PWM_TYPE` | Output |
|---|---|
| 0 Normal, 8 PWMRange, 9 PWMAngle | `RC_SPEED` 50–490 Hz, pulse width 400–2100 µs |
| 1 OneShot | The same continuous PWM as above (the chip has no one-shot trigger) |
| 2 OneShot125 | Pulse width 125–250 µs (1000–2000 divided by 8) in 0.25 µs steps; fixed 1 kHz continuous output, `RC_SPEED` is ignored |
| 3 Brushed | `RC_SPEED` 10–32 kHz |
| 4 DShot150, 5 DShot300, 6 DShot600 | Frames sent through GPIO, all outputs at once; see *DShot* |

- Brushed and OneShot125 share the PWM clock (brushed uses 40 MHz, OneShot125 the chip's internal 4 MHz oscillator), so only one of them works in a given boot; the other stops its output and reports the reason as STATUSTEXT.
- Not supported: DShot1200 (7) and LED outputs (NeoPixel, ProfiLED).
- Calibrate ESCs after fitting them.

## When the main loop stalls

- While armed, if the main loop has not completed a pass for more than 100 ms (for example because it is stuck), all PWM outputs stop: normal PWM sends no more pulses and brushed motors get duty 0. ESCs lose their signal and stop, brushed motors stop, and servos go limp.
- On DShot, the resent throttle becomes 0 and DShot commands queued before the stall are not sent.
- Values from before the stall, including values written but not yet sent, are not used again. After the main loop recovers, only the channels the main program writes again resume, with the new values; an empty output does not bring old values back.

## DShot

- The CPU sends DShot frames by writing the set and clear registers of the GPIO port, on all outputs at once (`dshot_shim.c`).
- Interrupts are masked while each frame is sent: about 107 µs at DShot150, 53 µs at DShot300 and 27 µs at DShot600. DShot300 is recommended.
- Frames go out at 800 Hz or more, at most about 1.25 ms apart. Each output from the main program sends one frame, and a timer checks every 1 ms and resends the last frame when more than 250 µs have passed since the previous one. With Copter's 400 Hz main loop that is about 1,300 frames a second, so interrupts are masked for an estimated 14% of the time at DShot150, 7% at DShot300 and 3.5% at DShot600. `SERVO_DSHOT_RATE` is not used.
- The first switch to DShot measures the CPU clock with the CPU cycle counter. If the counter is missing or the clock is not 500 MHz, DShot is not sent and the reason is sent as STATUSTEXT.
- If the cycle counter stalls, goes backwards or jumps too far in the middle of a frame (for example when an interrupt that cannot be masked holds up the loop), all pins are pulled low and that frame is dropped; the next output is sent as usual. The first time this happens, a warning goes to the ground station as STATUSTEXT. The falling edge of a 0 bit may be at most 50 ns late.
- All DShot outputs have to be on the same GPIO port. On the HUB 8735 ultra, the PWM pins on the headers are all on port F.
- Once an output switches to DShot, its pin stays GPIO for the rest of the boot. Changing `MOT_PWM_TYPE` needs a reboot.
- The `RCOut:` message at boot shows each output's mode, for example `RCOut: DS300:1-4`.
- Supported: DShot commands sent through `send_dshot_command` (for example beeps from the ESCs), and the BLHeli_S bit widths (`SERVO_DSHOT_ESC`).
- Not supported: bidirectional DShot (RPM readback), 3D and reversed channel settings (`SERVO_BLH_3DMASK`, `SERVO_BLH_RVMASK`), and flashing ESC firmware through the flight controller (BLHeli passthrough). Channels in `SERVO_BLH_3DMASK` are held at 0 (not turning) and a warning is given once, because sending them as normal throttle would mean about half throttle forward.

## ESC telemetry

- The telemetry wire (TLM) of BLHeli_32, AM32 and similar ESCs goes to the RX of a serial port; only RX is needed. That port's `SERIALn_PROTOCOL` is set to 16, and the baud rate is set to 115200 automatically.
- `MOT_PWM_TYPE` has to be DShot, and the motor pole count goes in `SERVO_BLH_POLES`.
- The ESCs' current and voltage can serve as a battery monitor with `BATTn_MONITOR` 9, and are also in the `ESC` log and the MAVLink `ESC_TELEMETRY` messages.
- A custom board that does not need it leaves it out with `undef HAL_SUPPORT_RCOUT_SERIAL` in its overlay.
