# 建置韌體

[English](building.md)

怎麼為 RTL8735B 開發板建置 ArduPilot Copter 的映像。從拿到開發板到連上地面站的第一次完整流程，以及每一步應該看到什麼，見[從零開始](../GETTING_STARTED.zh-TW.md)。

## 需求

- Ubuntu 22.04，或 WSL 裡的 Ubuntu 22.04。
- 原始碼、工具與 SDK 至少要 7 GB 的磁碟空間。
- 安裝與建置時要有網路：建置會自己下載 `gtest`、`littlefs` 兩個子模組。
- Realtek RTL8735B SDK，提交 `d1b6426bb69339a609df4a3b7cd0059392999ddf`（[ambiot/ambpro2_sdk](https://github.com/ambiot/ambpro2_sdk)），以及它附的 Realtek ASDK 工具鏈。下面的安裝步驟會取得兩者。

## 取得原始碼

```bash
cd ~
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
```

- 在 WSL 裡，用 Linux 的 `git` clone 到 Linux 的家目錄。放在 `/mnt/c` 底下建置會非常慢；Windows 的 Git 會改掉換行字元，腳本就壞了。
- clone 時不加 `--recurse-submodules`：安裝步驟只初始化建置需要的三個子模組。

## 安裝工具與 SDK

在儲存庫根目錄執行：

```bash
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$PWD/../ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
```

- 第一個腳本以 `sudo` 安裝套件。
- SDK 放在儲存庫旁的 `../ambpro2_sdk`，解開後約 1.8 GB。放在其他位置時，執行 `Tools/scripts/rtl8735b_get_sdk.sh <SDK目錄>`，以腳本結尾印出的 `export` 指令設定 PATH，並設定環境變數 `AMEBAPRO2_SDK=<SDK目錄>`。
- `arm-none-eabi-gcc --version` 的第一行有 `Realtek ASDK`。系統套件的 `arm-none-eabi-gcc` 建不出本移植。PATH 沒設時，configure 會印 `Could not find the program ['arm-none-eabi-ar']`。
- 開新的終端機時，要再執行一次 `source ~/.venv-rtl8735b/bin/activate` 與 `export PATH=…` 那一行。

## 設定 Wi-Fi 設定檔

要讓開發板連自己的熱點，建置前把 `RTL8735B_WIFI_CONFIG` 指向設定檔；沒有設定時，開發板連[預設熱點](wifi.zh-TW.md#預設熱點) `ardupilot`。設定檔的格式見 [Wi-Fi](wifi.zh-TW.md#設定檔)。

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

## 為經 Wi-Fi 更新建置

[經 Wi-Fi 更新](flashing.zh-TW.md#經-wi-fi-更新)要韌體裡有開發板的應用程式 `app/ota` 與公鑰。

1. 用 ArduPilot 的 `Tools/scripts/signing/generate_keys.py` 產生一組金鑰，它需要 `pymonocypher` 3.1.3.2：

   ```bash
   python3 -m pip install pymonocypher==3.1.3.2
   python3 Tools/scripts/signing/generate_keys.py mykey
   ```

   產生 `mykey_private_key.dat` 與 `mykey_public_key.dat`。私鑰在電腦上簽映像；韌體裡只有公鑰。

2. 疊加檔加上應用程式：

   ```text
   env RTL8735B_APP_DIR libraries/AP_HAL_RTL8735B/app/ota
   ```

3. 把 `RTL8735B_OTA_PUBLIC_KEY` 指向公鑰檔，再照下面 configure 與建置：

   ```bash
   export RTL8735B_OTA_PUBLIC_KEY=~/mykey_public_key.dat
   ```

- 建置印出 `OTA 公鑰：取自 <路徑>`。
- 給的是私鑰檔，或不是 `generate_keys.py` 產生的公鑰檔時，建置停止。
- 沒有設 `RTL8735B_OTA_PUBLIC_KEY` 時，韌體沒有公鑰，開發板拒收所有映像。
- 公鑰只編進有開發板應用程式的韌體。沒有應用程式時，建置印出 `OTA 公鑰：沒有開發板的應用程式（env RTL8735B_APP_DIR），公鑰不會編進韌體`。

## configure 與建置

```bash
./waf configure --board hub8735ultra
./waf copter
```

- `--board` 是 `hwdef/` 裡的一個板型：[`hub8735ultra`](../hwdef/hub8735ultra/README.zh-TW.md)、[`hub8735ultra-expansion`](../hwdef/hub8735ultra-expansion/README.zh-TW.md)、[`amb82-mini`](../hwdef/amb82-mini/README.zh-TW.md)、[`rtl8735b-empty`](../hwdef/rtl8735b-empty/README.zh-TW.md)。
- 除了 `hub8735ultra-expansion`，板型都沒有感測器。感測器、安裝方向與預設參數寫在疊加檔，在 configure 那一行以 `--extra-hwdef <檔案>` 加上；見[描述自己的開發板](custom-boards.zh-TW.md)。
- 建置開頭附近，用到設定檔時印 `Wi-Fi STA: 帳密取自 <路徑>`，沒有時印 `Wi-Fi STA: 未設定（…），開發板連板型的預設熱點…`。
- 最後一行是 `'copter' finished successfully`。中間有 `warning:`、`sed: can't read ...`、`JSON header - entry : fail` 的行，都來自 SDK，不影響結果。
- 映像是 `build/<板型>/sdk_build/flash_ntz.bin`，例如 `build/hub8735ultra/sdk_build/flash_ntz.bin`。
- 改了設定檔，再執行一次 `./waf copter` 即可。改了 `FLASH_LAYOUT`，要重新 configure。

映像寫進開發板的方法見[燒錄韌體](flashing.zh-TW.md)。
