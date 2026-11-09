# Build the firmware

[繁體中文](building.zh-TW.md)

How to build an ArduPilot Copter image for an RTL8735B board. The first run from a new board to a connected ground station, with what to expect at each step, is [Getting started](../GETTING_STARTED.md).

## Requirements

- Ubuntu 22.04, or Ubuntu 22.04 in WSL.
- At least 7 GB of disk for the source, the tools and the SDK.
- A network connection during setup and the build: the build downloads the `gtest` and `littlefs` submodules by itself.
- The Realtek RTL8735B SDK at commit `d1b6426bb69339a609df4a3b7cd0059392999ddf` ([ambiot/ambpro2_sdk](https://github.com/ambiot/ambpro2_sdk)), with the Realtek ASDK toolchain that comes with it. The setup below fetches both.

## Get the source

```bash
cd ~
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
```

- In WSL, clone into the Linux home directory with the Linux `git`. Under `/mnt/c` the build is very slow, and Windows Git changes the line endings, which breaks the scripts.
- Clone without `--recurse-submodules`: the setup initializes only the three submodules the build needs.

## Set up the tools and the SDK

From the root of the repository:

```bash
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$PWD/../ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
```

- The first script installs packages with `sudo`.
- The SDK goes next to the repository, in `../ambpro2_sdk`, about 1.8 GB unpacked. To put it somewhere else, run `Tools/scripts/rtl8735b_get_sdk.sh <SDK directory>`, set PATH with the `export` command the script prints at the end, and set the environment variable `AMEBAPRO2_SDK=<SDK directory>`.
- The first line of `arm-none-eabi-gcc --version` contains `Realtek ASDK`. The `arm-none-eabi-gcc` from the system packages does not build this port. Without PATH set, configure prints `Could not find the program ['arm-none-eabi-ar']`.
- A new terminal needs `source ~/.venv-rtl8735b/bin/activate` and the `export PATH=…` line again.

## Set the Wi-Fi settings file

To have the board join your own hotspot, point `RTL8735B_WIFI_CONFIG` at a settings file before building; without it, the board joins the [default hotspot](wifi.md#default-hotspot) `ardupilot`. The file's format is in [Wi-Fi](wifi.md#settings-file).

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

## Configure and build

```bash
./waf configure --board hub8735ultra
./waf copter
```

- `--board` is one of the board types in `hwdef/`: [`hub8735ultra`](../hwdef/hub8735ultra/README.md), [`hub8735ultra-expansion`](../hwdef/hub8735ultra-expansion/README.md), [`amb82-mini`](../hwdef/amb82-mini/README.md), [`rtl8735b-empty`](../hwdef/rtl8735b-empty/README.md).
- Apart from `hub8735ultra-expansion`, the board types have no sensors. Sensors, mounting orientation and default parameters go in an overlay, added with `--extra-hwdef <file>` on the configure line; see [Describe your own board](custom-boards.md).
- Near the start, the build prints `Wi-Fi STA: 帳密取自 <path>` ("credentials taken from …") when it uses the settings file, or `Wi-Fi STA: 未設定（…），開發板連板型的預設熱點…` ("not set; the board joins the board type's default hotspot").
- The last line is `'copter' finished successfully`. Lines with `warning:`, `sed: can't read ...` and `JSON header - entry : fail` come from the SDK and do not affect the result.
- The image is `build/<board type>/sdk_build/flash_ntz.bin`, for example `build/hub8735ultra/sdk_build/flash_ntz.bin`.
- After changing the settings file, `./waf copter` again is enough. After changing `FLASH_LAYOUT`, run configure again.

The image is written to the board as in [Flash the firmware](flashing.md).
