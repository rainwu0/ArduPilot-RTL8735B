# Board applications

[繁體中文](board-applications.zh-TW.md)

A custom board can build SDK-side C code, a board application, into the same firmware. This page describes the interface between the application and the firmware; the declarations and full descriptions are in `app/rtl8735b_app.h`.

## Building an application in

One line in the overlay hwdef names the directory:

```text
env RTL8735B_APP_DIR <directory>
```

- A relative path is taken from the root of this repository; if the directory does not exist, configure fails.
- The `.c` files in the directory are compiled together with the SDK sources, with the SDK's includes and macros; if the directory has no `.c` file, the build fails.
- The application uses only the SDK and `app/rtl8735b_app.h`, not ArduPilot's internals. Its task priorities are 3 or lower: the ArduPilot main thread is 4, see `Scheduler.h`.
- Without this line, the firmware contains no application.

## Functions the firmware calls

`rtl8735b_app_init` must be defined: if it is not, linking fails (`undefined reference to rtl8735b_app_init`), so a missing application never silently does nothing. The other three are optional; when they are not defined, the firmware uses empty functions, and `rtl8735b_app_arming_blocked` returns 0.

| Function | When it is called | Restrictions |
|---|---|---|
| `rtl8735b_app_init` | Once per boot: after the ArduPilot task is created and before the scheduler starts; no ArduPilot code has run yet | Only create tasks; `printf` and direct flash access through the SDK are fine; nothing that needs the scheduler to complete can be waited for |
| `rtl8735b_app_after_setup` | Once per boot: in the ArduPilot main task, after `setup()` finishes and before the first main loop | Only create tasks or set variables; blocking delays the start of the main loop |
| `rtl8735b_app_arming_blocked` | While disarmed, from the main task's arming checks and status reports; returns non-zero to block arming | Only read variables: no blocking, no locks |
| `rtl8735b_app_before_reset` | Right before the firmware resets itself (`Scheduler::reboot()`, for example a reboot command from the ground station or a parameter that needs a reboot); the outputs are already safe, and the reset follows when it returns | May be called from any task: no blocking, no locks, no printing |

A non-zero return from `rtl8735b_app_arming_blocked` makes the HAL report the safety switch as not pressed. On Copter a normal arm is then refused with `Safety Switch` whatever `ARMING_SKIPCHK` says; only a forced arm skips the check. On other vehicles the safety switch pre-arm check refuses it, unless `ARMING_SKIPCHK` skips that check or the arm is forced. While it blocks, Copter's motor test is refused too, and the ground station shows the motor outputs as not enabled. It can be called from several tasks, and returns a value read in one go (for example a `volatile uint8_t`). Without an application it returns 0.

`rtl8735b_app_before_reset` is for GPIOs that double as boot-mode pins. The level of PA5 at reset decides whether the chip enters download mode. An application that drives PA5 as an output (D14 on the HUB 8735 ultra, marked Sensor power on in `variant.h`) puts the pin back to its boot-time state here; otherwise the chip may stay in download mode after the firmware reboots and not start. This covers only reboots done by the firmware itself: watchdog and crash resets do not come through here, and an application that calls the SDK's `sys_reset()` itself handles these pins first.

## Functions the application can call

| Function | Returns or does | Restrictions |
|---|---|---|
| `rtl8735b_ardupilot_armed()` | 1: armed (`AP_HAL::Util::get_soft_armed()`, motor tests included); 0: disarmed | Reads variables only; does not block or take locks; any task or FreeRTOS timer callback may call it |
| `rtl8735b_ardupilot_system_initialized()` | 1: `setup()` has finished; 0: not yet | Same as above |
| `rtl8735b_ardupilot_ms_since_main_loop()` | Milliseconds since the main loop last finished, computed at the time of the call, so it keeps growing while the main loop is stuck; `UINT32_MAX` before the first loop finishes | Same as above |
| `rtl8735b_ardupilot_outputs_safe()` | Stops all PWM outputs and puts the safety switch back to safe (`RCOutput::force_safety_on()`), for example before the application resets with `sys_reset()`; the vehicle has to be disarmed before such a reset | Waits for the main task to release the output lock: any FreeRTOS task may call it, but not an interrupt or a FreeRTOS timer callback |

## Compile definitions

The application's `.c` files also get these definitions, with values from the hwdef's `FLASH_LAYOUT` (see [Flash layout](flash-layout.md)):

- `RTL8735B_FLASH_LAYOUT_NAME`: the layout name as a string, for example `"realtek-ota"`.
- `RTL8735B_FLASH_FW1_ADDR`, `RTL8735B_FLASH_FW1_LEN`, `RTL8735B_FLASH_FW2_ADDR`, `RTL8735B_FLASH_FW2_LEN`: the address and length in bytes of fw1 and fw2 in this image's partition table.

The firmware's own sources do not get them.

## `examples/AppHost`

On a board without sensors, the `examples/AppHost` example runs the application: it starts only Wi-Fi and the main loop, does not initialize sensors or arm, and leaves the PWM outputs safe, so `setup()` finishes and the main loop runs. A normal vehicle image on a board without an IMU stops at inertial sensor initialization, and `rtl8735b_app_after_setup` is never called.

```bash
./waf configure --board rtl8735b-empty --extra-hwdef /path/to/myapp.dat
./waf --targets examples/AppHost
```

The status of board applications is in [Feature status](feature-status.md).
