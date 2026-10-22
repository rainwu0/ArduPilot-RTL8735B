# Getting started: ArduPilot on the HUB 8735 ultra

繁體中文：[GETTING_STARTED.md](GETTING_STARTED.md)

Work through this page once from the top, and at the end you will see the board's attitude in a ground station. Each step ends with what you see when it works. If what you see does not match, stop there and open an issue saying which step you are stuck on.

**Not flight-tested. Do not use it for flight.** This page only goes as far as connecting a ground station; do not connect motors, ESCs or a battery at any point.

## What you need

- **Board**: one HUB 8735 ultra, and a USB-C cable that **carries data**. With a charge-only cable, no serial port appears on the computer.
- **Sensor modules**: at least two, a GY-BMI160 (IMU) and a BMP280 (barometer). The GY-271 compass (QMC5883L) can wait. You also need jumper wires or soldering tools.
- **Computer**: a Windows PC with Ubuntu 22.04 in WSL. Building happens in WSL; flashing and the ground station are on the Windows side.
- **Wi-Fi hotspot**: the computer and the board both join the same one; 2.4 GHz is recommended. Whether 5 GHz works has not been verified. Use a hotspot you control, because MAVLink over Wi-Fi has no authentication at all.
- **Disk and time**: at least 7 GB of disk. From clone to a built image takes about 13 minutes: clone about 3.5 minutes, tools and SDK about 5 minutes, build about 3.3 minutes (depending on your network and computer).

**Why the sensors are required:**
- With nothing connected, boot stops at `Config Error: Baro: unable to initialise driver`. Wi-Fi joins the hotspot, but the ground station receives nothing. Wi-Fi is not broken; ArduPilot just does not go on without a barometer.
- With only the barometer, boot then stops at the IMU's Config Error: the ground station should connect and receive this error, but there is no attitude. This is inferred from reading the code and has not been verified on a board.
- So connect at least the barometer and the IMU.

## 1. Wiring

Wire as in [Connecting sensors](README.en.md#connecting-sensors) in the HAL README.

- Connect every module to 3.3 V and GND, never to 5 V: the RTL8735B's I/O is 3.3 V.
- 3.3 V and GND: use the 3.3 V and GND marked on the silkscreen next to the board's header pins. If unsure, see the schematic the vendor published: `HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf` in ideashatch/HUB-8735 `870a7e08de7a`.
- Mount the IMU as in the example: component side up, with the front at the end opposite the USB connectors. If you mount it differently, the attitude in the last step will be wrong; see "Mounting orientation" in *Connecting sensors*.

## 2. Get the source in WSL

Open a WSL Ubuntu terminal and **clone into WSL's own home directory**:

```bash
cd ~
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
```

The sensor settings (the overlay) are in the [example repository](https://github.com/rainwu0/ArduPilot-Hub8735Ultra/tree/v0.1.0/examples/bmi160-bmp280-qmc5883l). Clone it into your home directory too; the build uses it:

```bash
git clone --branch v0.1.0 https://github.com/rainwu0/ArduPilot-Hub8735Ultra.git ~/ArduPilot-Hub8735Ultra
```

- Do not clone under `/mnt/c`, and do not use Windows Git: the first makes the build very slow, and the second changes the line endings and breaks the scripts.
- Do not add `--recurse-submodules`: ArduPilot has many submodules you do not need. The next step installs only three; the build downloads two more by itself, `gtest` and `littlefs`, so you also need a network connection while building.

## 3. Install the tools and the SDK

In `~/ArduPilot-RTL8735B`, run:

```bash
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$HOME/ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
```

- The first line installs packages with `sudo`, so it asks for your WSL password.
- The SDK and the toolchain go next door, in `~/ambpro2_sdk`, about 1.8 GB unpacked.
- When you open a new terminal, a few settings have to be redone; see the end of step 4.

When it works: the first line of `arm-none-eabi-gcc --version` contains `Realtek ASDK`. If it does not, PATH is not set correctly, or the compiler from the system packages is being used, and that one does not work. Without PATH set, the configure that follows prints `Could not find the program ['arm-none-eabi-ar']`.

## 4. Wi-Fi settings file

In your WSL home directory, create a file, for example `~/wifi.txt`, with two lines:

```
ssid=your-hotspot-name
password=your-hotspot-password
```

- Everything after the equals sign is used exactly as written. **Do not add quotes, and do not leave extra spaces around the equals sign or at the end of a line**, or the quotes and spaces become part of the name or password. Spaces inside the name are fine.
- A stray quote or space gives **no** warning at build time. You only find out in step 7, from `SSID length N` in the boot messages: N is the number of bytes in the hotspot name, 1 for each letter or digit and 3 for each Chinese character.
- The password must be 8–63 characters, or 64 hexadecimal characters, and only printable ASCII (letters, digits, symbols and spaces), with no Chinese or other characters; for an open network, leave `password=` empty. If the length or the characters are wrong, the build fails and says why.
- Keep this file out of every git repository; you can `chmod 600 ~/wifi.txt`.

Before building, tell the build system where it is:

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

The settings file is read at build time. After changing it, just build again; there is no need to configure again.

The credentials are compiled into the firmware: do not share the images you build, and do not attach them when you report a problem.

**Every time you open a new terminal, run these four lines before building:**

```bash
cd ~/ArduPilot-RTL8735B
source ~/.venv-rtl8735b/bin/activate
export PATH="$HOME/ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

If you leave out the last line, the image you build has no Wi-Fi.

## 5. Build

```bash
./waf configure --board hub8735ultra --extra-hwdef ~/ArduPilot-Hub8735Ultra/examples/bmi160-bmp280-qmc5883l/overlay.dat
./waf copter
```

When it works:

- near the start there is `Wi-Fi STA: 帳密取自 <path to your settings file>` ("credentials taken from …");
- the last line is `'copter' finished successfully`, and the image is `build/hub8735ultra/sdk_build/flash_ntz.bin`.

Lots of `warning:` lines scroll past on the way, along with `sed: can't read ...` and `JSON header - entry : fail`. They all come from the SDK and do not affect the result; go by the last line.

**If you see `Wi-Fi STA: 未設定` ("not set") instead**: the build still succeeded, but the image has no Wi-Fi. Usually the path to the settings file is wrong, or this terminal did not run the `export`. Fix it and run `./waf copter` again.

Copy the image and the flashing tool to Windows. Replace `<you>` with your Windows user name; if you do not know it, run `ls /mnt/c/Users` in WSL to see it:

```bash
cp build/hub8735ultra/sdk_build/flash_ntz.bin /mnt/c/Users/<you>/Downloads/
cp "../ambpro2_sdk/tools/Pro2_PG_tool _v1.3.0.zip" /mnt/c/Users/<you>/Downloads/
```

The flashing tool's file name contains a space, hence the quotes.

## 6. Flash (on Windows)

1. **Unpack the flashing tool**: on Windows, unpack `Pro2_PG_tool _v1.3.0.zip` in `Downloads`. `uartfwburn.exe` is in the unpacked subfolder `Pro2_PG_tool _v1.3.0\` (if there is no such subfolder, look for it in the unpacked folder itself); open PowerShell in the folder that holds `uartfwburn.exe`.
2. **Connect the board and find the COM port**: the board has two USB-C ports, and only one of them goes to the CH340 USB-to-serial chip. When you plug that one in, `USB-SERIAL CH340 (COMx)` appears under "Ports (COM & LPT)" in Device Manager; that is the port to use, so note its COM number. The other port does not show `USB-SERIAL CH340`.
   - If it still does not appear after you try the other port, try another cable.
   - If the driver did not install by itself, download the CH340 driver from the website of the chip maker, WCH.
3. **Close programs that hold the serial port**: close every program that has this COM port open, such as terminals and ground stations.
4. **Enter download mode**: hold the upgrade button, press the reset button briefly, then release the upgrade button.
5. **Flash right away** (replace `COMx` with your number):

   ```powershell
   .\uartfwburn.exe -p COMx -f C:\Users\<you>\Downloads\flash_ntz.bin -b 2000000 -U
   ```

When it works: first you see `programing done.` (the tool's own spelling), and the last line is `download success`. After flashing, press reset once to boot.

If it fails:
- `uart COMx open fail`: common right after entering download mode. Wait a few seconds and run the same command again; there is no need to press the buttons again.
- `ping fail` or `ucfg fail`: most likely too much time passed after entering download mode; redo steps 4 and 5.

Do not unplug USB while flashing.

## 7. Watch the boot messages

1. Open **the same COM port** in any serial terminal program (for example PuTTY or Tera Term), set to 115200, 8N1.
2. **Open the terminal first, then press reset**, or you will miss the boot messages.

When it works, you see roughly this, in order:

```
== RAM Start ==
Storage: flash id ..., using 0xf80000-0xfa0000
WIFI initialized
WiFi: joining (SSID length N)
WiFi: up, local 192.168.x.x:14555
```

- N in `SSID length N` must equal the number of bytes in your hotspot name (1 for each letter or digit, 3 for each Chinese character). If it does not, the settings file has an extra quote or space.
- `WiFi: up` appears about 5 seconds after boot. The IP after it is the board's address; note it down.
- With a compass connected, an extra line `QMC5883L found on bus 1 ...` appears at about 17 seconds; without a compass it does not.
- **If you paste boot messages into a report, mask the `set ssid [...]` line first**: the SDK prints your hotspot name there.

If something is wrong:
- **`Config Error: Baro: unable to initialise driver` repeats every 5 seconds**: the barometer was not found. Check the barometer's wiring and 3.3 V, then press reset. v0.1.0 occasionally showed this error at boot for no reason, which v0.1.1 fixes; with v0.1.0, try pressing reset once first.
- **`WiFi: no STA credentials` appears**: the image has no Wi-Fi credentials; go back to step 5 and check for `帳密取自`.
- **`WiFi: up` never appears**: check the hotspot name and password; a 2.4 GHz hotspot is best.

## 8. Connect a ground station

1. Connect the computer to the same hotspot. The serial terminal can stay open; it does not affect Wi-Fi.
2. Until the board finds a ground station, it broadcasts heartbeats to UDP 14550, so ground station software listening on that port picks them up:
   - **QGroundControl**: listens on UDP 14550 by default; open it and wait for it to connect.
   - **Mission Planner**: choose UDP as the connection type at the top right, press Connect, and enter 14550 as the port.
3. The first time Windows receives UDP, the firewall may ask; allow it on "Private networks". If Windows has put this hotspot under "Public networks", allow it on "Public networks" instead, or change this hotspot to "Private" in the Windows network settings.

(The steps for these two ground stations have not yet been tried by following this page.)

When it works:

- the serial port shows an extra line, `WiFi: ground station <computer's IP>:14550`;
- the ground station downloads the parameters, a little over 900 of them;
- with the board level, roll and pitch are close to 0; lift the front (the end opposite the USB connectors) by about 45° and pitch becomes about +40°.

**If there are other flight controllers on the same hotspot, the ground station may connect to one of them.** The simplest way around this is to have only this board on the hotspot; if your ground station shows where the connection comes from, check that it is the IP you noted in step 7.

## What next

- How far each feature works: [Feature status](README.en.md#feature-status); the record of each board test: [BOARD_TESTS.md](BOARD_TESTS.md) (in Chinese).
- To use your own sensors or pins: [Custom boards](README.en.md#custom-boards).
- Stuck, or it all worked: either way, please open an issue and pick the "上板結果 / Board test report" template.
