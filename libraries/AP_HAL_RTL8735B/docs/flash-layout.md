# Flash layout

[繁體中文](flash-layout.zh-TW.md)

The partition table in `flash_ntz.bin` (the addresses and lengths of the firmware slots, ISP IQ, the NN model area and so on), and where parameters are stored.

## `FLASH_LAYOUT`

The layout is chosen by `FLASH_LAYOUT` in hwdef, added with `--extra-hwdef`:

```text
FLASH_LAYOUT sdk-d1b6426
```

| Layout | fw1 | fw2 | Image limit | Source |
|---|---|---|---|---|
| `realtek-ota` (default) | 0x060000, 4 MiB | 0x520000, 4 MiB | 4,194,304 bytes | `amebapro2_partitiontable_OTA.json` of the Realtek Arduino package with OTA Mode=Enable (Ameba-AIoT/ameba-arduino-pro2 `93d6351`), the same as the example partitions in Ameba-AIoT/ameba-rtos-pro2 `eb5c090` |
| `sdk-d1b6426` | 0x080000, 3.5 MiB | 0x4C0000, 3.5 MiB | 3,670,016 bytes | `amebapro2_partitiontable.json` of the example project in ambpro2_sdk `d1b6426` |
| `arduino-default` | 0x060000, 4 MiB | 0x520000, 64 KiB | 4,194,304 bytes | `amebapro2_partitiontable.json` of the same Arduino package with OTA Mode=Disable |

- The status of each layout is in [Feature status](feature-status.md).
- Flashing a `flash_ntz.bin` from a default build changes the board's partition table to `realtek-ota`. The Arduino packages for the HUB 8735 ultra (vendor package 4.1.1, released in ideashatch/HUB-8735 `870a7e0`) and the AMB82-mini (Ameba-AIoT/ameba-arduino-pro2 `93d6351`) use this layout with OTA Mode=Enable, and so does the newer SDK (Ameba-AIoT/ameba-rtos-pro2 `eb5c090`). `sdk-d1b6426` keeps the original partitions of SDK `d1b6426`.
- The build starts from the SDK's partition table and rewrites only the address and length of each record.
- The image limit is the length of fw1, because the firmware is written to fw1. If `firmware_ntz.bin` exceeds the limit, or the slot records in the built `flash_ntz.bin` differ from the layout, the build fails.
- This repository does not provide Wi-Fi firmware updates and does not write fw2; fw2 is only a record in the partition table.
- The layout is read at `./waf configure`: after changing `FLASH_LAYOUT` in hwdef or an overlay, configure again.

## Parameter storage

- Parameters are stored at 0xF80000–0xFA0000, the same in all three layouts. The NOR file system area (0xF64000–0x1000000) of the Arduino package (`93d6351`) and the newer SDK (`eb5c090`) covers this range; it conflicts only when the flash file system is enabled, and this port does not enable it.
- Parameters are stored with ArduPilot's `AP_FlashStorage`, alternating between two 64 KiB sectors. Losing power in the middle of a write loses only that last write.
- After one sector fills and storage moves to the other, the next boot erases the full sector (16 erases of 4 KiB). Losing power or pressing reset during that time brings all parameters back to their defaults on the next boot, and the pre-arm checks show the missing calibration. A parameter file saved with the ground station can be loaded back.
- An erase can also happen while running (a second sector change in the same boot, or rebuilding storage after a write failure), and it starts only while disarmed. While it runs, the pre-arm checks report `Param storage failed` and a normal arm is refused; this clears when the erase finishes. A forced arm skips this check.

Why the erase window exists is in [Design notes](design.md#parameter-storage-and-power-loss).
