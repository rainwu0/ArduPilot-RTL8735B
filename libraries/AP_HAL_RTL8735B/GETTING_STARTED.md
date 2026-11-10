# Getting started: ArduPilot on the HUB 8735 ultra

[繁體中文](GETTING_STARTED.zh-TW.md)

Work through this page once from the top, and at the end you will see the board's attitude in a ground station. Each step ends with what you see when it works. If what you see does not match, stop at that step and open an issue saying where you are stuck.

This page goes as far as a connected ground station, with no motors, ESCs or battery at any point.

## What you need

- **Board**: one HUB 8735 ultra, and a USB-C cable that **carries data**. With a charge-only cable, no serial port appears on the computer.
- **Sensor modules**: a GY-BMI160 (IMU) and a BMP280 (barometer). The GY-271 compass (QMC5883L) can wait. You also need jumper wires or soldering tools.
- **Computer**: a Windows PC with Ubuntu 22.04 in WSL. Building happens in WSL; flashing and the ground station are on the Windows side.
- **Wi-Fi hotspot**: the computer and the board both join the same one; 2.4 GHz is recommended. You can open the default hotspot on a phone (name `ardupilot`, password `ardupilot1`) or use your own hotspot, see step 4. MAVLink over Wi-Fi has no authentication at all: any device that can join the hotspot can send to the board.
- **Disk and time**: at least 7 GB of disk. From clone to a built image takes about 13 minutes: clone about 3.5 minutes, tools and SDK about 5 minutes, build about 3.3 minutes (depending on your network and computer).

Both the barometer and the IMU are needed: with nothing connected, boot stops at `Config Error: Baro: unable to initialise driver`, and although Wi-Fi joins the hotspot, the ground station receives nothing over it.

## 1. Wiring

Wire as in the [Wiring Diagram](hwdef/hub8735ultra/README.md#wiring-diagram) of the HUB 8735 ultra README.

- Connect every module to 3.3 V and GND: the RTL8735B's I/O is 3.3 V.
- 3.3 V and GND: use the 3.3 V and GND marked on the silkscreen next to the board's header pins. If unsure, see the schematic the vendor published: `HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf` in ideashatch/HUB-8735 `870a7e08de7a`.
- Mount the IMU component side up, with the front at the end opposite the USB connectors. If you mount it differently, the attitude in the last step will be wrong; see "Mounting orientation" in the same section.

When it works: every module's VCC goes to 3.3V (JA2.1) and its GND to a GND pin, and each signal wire goes to the pin in the table of the wiring diagram. The board is not powered yet.

## 2. Get the source in WSL

Open a WSL Ubuntu terminal and **clone into WSL's own home directory**:

```bash
cd ~
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
```

- Use the git in WSL and clone into WSL's home directory: under `/mnt/c` the build is very slow, and Windows Git changes the line endings, which breaks the scripts.
- Clone without `--recurse-submodules`: ArduPilot has many submodules you do not need. The next step installs only three; the build downloads two more by itself, `gtest` and `littlefs`, so you also need a network connection while building.

When it works: `pwd` shows `/home/<your WSL user name>/ArduPilot-RTL8735B`, not a path under `/mnt/c`.

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

When it works: the first line of `arm-none-eabi-gcc --version` contains `Realtek ASDK`. If it does not, PATH is not set correctly, or the compiler from the system packages is being used, and that one does not build this port. Without PATH set, the configure that follows prints `Could not find the program ['arm-none-eabi-ar']`.

## 4. Wi-Fi

The board joins a hotspot in station mode, and the computer joins the same one. Pick one of two ways:

- **[Default hotspot](docs/wifi.md#default-hotspot)**: no settings file. Open a hotspot on a phone or computer named `ardupilot` with the password `ardupilot1`.
- **Your own hotspot**: write a settings file and point the build system at it, as below.

With the default hotspot, the only part of this step you need is the three lines for every new terminal at the end.

Settings file: in your WSL home directory, create a file, for example `~/wifi.txt`, with two lines:

```text
ssid=your-hotspot-name
password=your-hotspot-password
```

- Everything after the equals sign is used exactly as written: **quotes, and extra spaces around the equals sign or at the end of a line, become part of the name or password**. Spaces inside the name are fine.
- A stray quote or space gives **no** warning at build time. You only find out in step 7, from `SSID length N` in the ground station's messages: N is the number of bytes in the hotspot name, 1 for each letter or digit and 3 for each Chinese character.
- The password is 8–63 characters, or 64 hexadecimal characters, of printable ASCII only (letters, digits, symbols and spaces); for an open network, leave `password=` empty. If the length or the characters are wrong, the build fails and says why.
- Keep this file outside every git repository; you can `chmod 600 ~/wifi.txt`.

Before building, tell the build system where it is:

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

The settings file is read at build time. After changing it, just build again; there is no need to configure again.

With a settings file, the credentials are compiled into the firmware, and anyone who has the image may be able to read them. For an image to share or to attach to an issue, build another one without `RTL8735B_WIFI_CONFIG`.

**Every time you open a new terminal, run these three lines before building:**

```bash
cd ~/ArduPilot-RTL8735B
source ~/.venv-rtl8735b/bin/activate
export PATH="$HOME/ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
```

With your own hotspot, add this line too:

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

If you leave it out, the image you build joins the default hotspot `ardupilot`, not yours.

When it works: with your own hotspot, `ls -l $RTL8735B_WIFI_CONFIG` lists your settings file, with `-rw-------` at the start if you ran `chmod 600`; with the default hotspot, `echo $RTL8735B_WIFI_CONFIG` prints an empty line.

## 5. Build

```bash
./waf configure --board hub8735ultra-expansion
./waf copter
```

When it works:

- near the start there is a Wi-Fi line: with your own hotspot it is `Wi-Fi STA: 帳密取自 <path to your settings file>` ("credentials taken from …"); with the default hotspot it is `Wi-Fi STA: 未設定（RTL8735B_WIFI_CONFIG=未指定），開發板連板型的預設熱點…` ("not set; the board joins the board type's default hotspot");
- the last line is `'copter' finished successfully`, and the image is `build/hub8735ultra-expansion/sdk_build/flash_ntz.bin`.

Lots of `warning:` lines scroll past on the way, along with `sed: can't read ...` and `JSON header - entry : fail`. They all come from the SDK and do not affect the result; go by the last line.

**If you use your own hotspot but see `Wi-Fi STA: 未設定` ("not set")**: the image joins the default hotspot, not yours. Usually the path to the settings file is wrong, or this terminal did not run the `export`. Fix it and run `./waf copter` again.

Copy the image and the flashing tool to Windows. Replace `<you>` with your Windows user name; if you do not know it, run `ls /mnt/c/Users` in WSL to see it:

```bash
cp build/hub8735ultra-expansion/sdk_build/flash_ntz.bin /mnt/c/Users/<you>/Downloads/
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

When it works: first you see `programing done.` (the tool's own spelling), and the last line is `download success`. After flashing, press reset once to boot. Keep USB connected until `download success`.

If it fails:

- `uart COMx open fail`: common right after entering download mode. Wait a few seconds and run the same command again; there is no need to press the buttons again.
- `ping fail` or `ucfg fail`: most likely too much time passed after entering download mode; redo steps 4 and 5.

## 7. Check the boot and Wi-Fi

When the board boots, the HAL hands this COM port to MAVLink (SERIAL0) early on: a serial terminal shows only the first few boot lines, followed by binary data. Watch the boot from the ground station's messages instead.

1. Connect a ground station to the board's COM port at 115200 baud. In Mission Planner, choose `COMx` and `115200` at the top right and press Connect.
2. Once it is connected, press reset. The board restarts, and the ground station's messages (the Messages tab in Mission Planner) show, in order:

   ```text
   WiFi: joining the board's default hotspot (SSID length 9)
   WiFi: up, local 192.168.x.x:14555
   ```

- The first line is what the default hotspot shows. With your own hotspot it is `WiFi: joining (SSID length N)`: N must equal the number of bytes in your hotspot name (1 for each letter or digit, 3 for each Chinese character), and if it does not, the settings file has an extra quote or space.
- The IP after `WiFi: up` is the board's address; note it down.
- These lines go only to a ground station that is already connected: connect it before pressing reset, and if you missed them, press reset again. You can also find the board's IP in the hotspot's list of devices; the Windows mobile hotspot, for example, lists the connected devices.

If something is wrong:

- **`Config Error: Baro: unable to initialise driver` appears every 5 seconds**: the barometer was not found. Check the barometer's wiring and 3.3 V, then press reset.
- **`WiFi: no STA credentials in this image; WiFi MAVLink port disabled` appears**: Wi-Fi is turned off in this image: there is no settings file, and an overlay set `WIFI_SSID_STATION` to `""` or undefined it.
- **`WiFi: join failed (…)` appears**: the board cannot join the hotspot. It tries again 1 s after each failure, and shows the same failure again at most once every 30 seconds. Check that the hotspot is on, and its name and password (the default hotspot is `ardupilot`, `ardupilot1`); a 2.4 GHz hotspot is best.

The status of SERIAL0 and of the default hotspot is in [Feature status](docs/feature-status.md).

## 8. Connect a ground station

1. Connect the computer to the same hotspot. If the ground station is still connected to the COM port, disconnect it first (Disconnect in Mission Planner).
2. Until the board finds a ground station, it broadcasts heartbeats to UDP 14550, so ground station software listening on that port picks them up:
   - **QGroundControl**: listens on UDP 14550 by default; open it and wait for it to connect.
   - **Mission Planner**: choose UDP as the connection type at the top right, press Connect, and enter 14550 as the port.
3. The first time Windows receives UDP, the firewall may ask; allow it on "Private networks". If Windows has put this hotspot under "Public networks", allow it on "Public networks" instead, or change this hotspot to "Private" in the Windows network settings.

When it works:

- the ground station downloads the parameters, a little over 900 of them;
- with the board level, roll and pitch are close to 0; lift the front (the end opposite the USB connectors) by about 45° and pitch becomes about +40°.

The board type also sets a battery monitor on D2 and D9 and a GPS on SERIAL1 (D8, D15). With nothing connected there, set `BATT_MONITOR` and `GPS1_TYPE` to 0 in the ground station's parameters, then reboot the board.

**If there are other flight controllers on the same hotspot, the ground station may connect to one of them.** The simplest way around this is to have only this board on the hotspot; if your ground station shows where the connection comes from, check that it is the IP you noted in step 7.

## What next

- To build the hardware of the T5 quadcopter: the [hub8735ultra-expansion README](hwdef/hub8735ultra-expansion/README.md), up to [Set by you](hwdef/hub8735ultra-expansion/README.md#set-by-you); then ArduPilot's [First Flight with Copter](https://ardupilot.org/copter/docs/flying-arducopter.html).
- How far each feature works: [Feature status](docs/feature-status.md); the record of each board test: [BOARD_TESTS.md](BOARD_TESTS.md) (in Chinese).
- To use your own sensors or pins: [Describe your own board](docs/custom-boards.md).
- All the building and flashing options: [Build the firmware](docs/building.md), [Flash the firmware](docs/flashing.md).
- Stuck, or it all worked: either way, please open an issue and pick the "Board test report / 上板結果" template.
