# 從零開始：在 HUB 8735 ultra 上跑 ArduPilot

[English](GETTING_STARTED.md)

照這一頁從頭做一次，最後會在地面站上看到開發板的姿態。每一步最後都寫了「成功時」會看到什麼。對不上就先停在那一步，開 issue 說卡在哪裡。

這一頁只做到地面站連上，全程不接馬達、電調與電池。

## 你需要

- **開發板**：一塊 HUB 8735 ultra，加一條**能傳資料**的 USB-C 線。只能充電的線接上之後，電腦不會出現序列埠。
- **感測器模組**：GY-BMI160（IMU）和 BMP280（氣壓計）各一。羅盤 GY-271（QMC5883L）可以之後再接。另外準備杜邦線或焊接工具。
- **電腦**：一台 Windows，裝好 WSL 的 Ubuntu 22.04。建置在 WSL 裡做，燒錄和地面站在 Windows 那邊。
- **Wi-Fi 熱點**：電腦和開發板都連同一個，建議用 2.4 GHz。可以用手機開預設熱點（名稱 `ardupilot`、密碼 `ardupilot1`），或用自己的熱點，見第 4 步。Wi-Fi 上的 MAVLink 沒有任何驗證，連得上這個熱點的裝置都送得到開發板。
- **磁碟和時間**：至少 7 GB 磁碟。從 clone 到建好映像約 13 分鐘：clone 約 3.5 分鐘、裝工具與 SDK 約 5 分鐘、建置約 3.3 分鐘（依網路與電腦而定）。

氣壓計與 IMU 都要接：什麼都不接時，開機停在 `Config Error: Baro: unable to initialise driver`，Wi-Fi 會連上熱點，但地面站經 Wi-Fi 收不到任何東西。

## 1. 接線

照 HUB 8735 ultra README 的[接線圖](hwdef/hub8735ultra/README.zh-TW.md#接線圖)接。

- 模組一律接 3.3 V 和 GND：RTL8735B 的 I/O 是 3.3 V。
- 3.3 V 與 GND：照開發板排針旁絲印標的 3.3 V 和 GND 接。不確定的話，看板商公開的原理圖：ideashatch/HUB-8735 `870a7e08de7a` 的 `HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf`。
- IMU 元件面朝上擺，前方是 USB 接頭的另一端。擺法不同的話，最後一步的姿態方向會不對，見同一節的「安裝方向」。

成功時：每個模組的 VCC 接在 3.3V（JA2.1）、GND 接在一支 GND 腳，每條訊號線接在接線圖表格裡的那支腳。這時開發板還沒通電。

## 2. 在 WSL 裡取得原始碼

開 WSL 的 Ubuntu 終端機，**在 WSL 自己的家目錄裡 clone**：

```bash
cd ~
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
```

- 用 WSL 裡的 git，clone 在 WSL 的家目錄：放在 `/mnt/c` 底下建置會很慢，Windows 的 Git 會把換行改掉，腳本會壞。
- clone 時不加 `--recurse-submodules`：ArduPilot 的子模組很多，用不到。下一步只裝三個，建置時會再自己下載 `gtest`、`littlefs` 兩個，所以建置時也要連網。

成功時：`pwd` 顯示 `/home/<你的 WSL 帳號>/ArduPilot-RTL8735B`，不是 `/mnt/c` 底下的路徑。

## 3. 安裝工具與 SDK

在 `~/ArduPilot-RTL8735B` 裡執行：

```bash
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$HOME/ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
```

- 第一行會用 `sudo` 裝套件，要輸入 WSL 的密碼。
- SDK 和工具鏈會放在旁邊的 `~/ambpro2_sdk`，解開後約 1.8 GB。
- 開新的終端機時要重做幾行設定，見第 4 步的最後。

成功時：`arm-none-eabi-gcc --version` 的第一行有 `Realtek ASDK`。沒有的話，就是 PATH 沒設好，或用到了系統套件的編譯器，那個建不出本移植。PATH 沒設時，下一步的 configure 會印出 `Could not find the program ['arm-none-eabi-ar']`。

## 4. Wi-Fi

開發板以 station 模式連到一個熱點，電腦也連同一個。兩種做法選一種：

- **[預設熱點](docs/wifi.zh-TW.md#預設熱點)**：不用設定檔。用手機或電腦開一個熱點，名稱 `ardupilot`、密碼 `ardupilot1`。
- **自己的熱點**：寫一個設定檔，建置時指給建置系統，做法在下面。

用預設熱點的話，這一步只要做最後的「每次開新的終端機」那三行。

設定檔：在 WSL 的家目錄建一個檔案，例如 `~/wifi.txt`，內容兩行：

```text
ssid=你的熱點名稱
password=你的熱點密碼
```

- 等號後面的內容會原樣使用：**引號，以及等號前後、行尾多打的空白，都會變成名稱或密碼的一部分**。名稱中間的空白沒關係。
- 多了引號或空白時，建置**不會**警告。要到第 7 步看地面站訊息裡的 `SSID length N` 才發現得了：N 是熱點名稱的位元組數，英數字一個算 1，中文一個字算 3。
- 密碼是 8–63 個字元，或 64 個十六進位字元，只能用可列印的 ASCII（英數字、符號與空白）；開放網路就把 `password=` 後面留空。長度或字元不對時，建置會失敗並說明原因。
- 這個檔案放在所有 git 儲存庫之外，可以 `chmod 600 ~/wifi.txt`。

在建置之前告訴建置系統它在哪裡：

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

設定檔在建置時才讀。改了內容，直接重新建置就好，不用重新 configure。

用設定檔時，帳密會編進韌體，拿到映像的人可能讀得出來。要分享或附在 issue 的映像，不設 `RTL8735B_WIFI_CONFIG` 另外建一份。

**之後每次開新的終端機，先做這三行再建置：**

```bash
cd ~/ArduPilot-RTL8735B
source ~/.venv-rtl8735b/bin/activate
export PATH="$HOME/ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
```

用自己的熱點時，再加這一行：

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

漏了這一行，建出來的映像會連預設熱點 `ardupilot`，不會連你的熱點。

成功時：用自己的熱點時，`ls -l $RTL8735B_WIFI_CONFIG` 列出你的設定檔，做過 `chmod 600` 的話開頭是 `-rw-------`；用預設熱點時，`echo $RTL8735B_WIFI_CONFIG` 印出空行。

## 5. 建置

```bash
./waf configure --board hub8735ultra-expansion
./waf copter
```

成功時：

- 開頭附近有 Wi-Fi 那一行：用自己的熱點時是 `Wi-Fi STA: 帳密取自 <你的設定檔路徑>`；用預設熱點時是 `Wi-Fi STA: 未設定（RTL8735B_WIFI_CONFIG=未指定），開發板連板型的預設熱點…`；
- 最後一行是 `'copter' finished successfully`，映像在 `build/hub8735ultra-expansion/sdk_build/flash_ntz.bin`。

中途會捲過很多 `warning:`，還有 `sed: can't read ...`、`JSON header - entry : fail` 這幾行。這些都來自 SDK，不影響結果，以最後一行為準。

**用自己的熱點卻看到 `Wi-Fi STA: 未設定`**：映像會連預設熱點，不會連你的熱點。通常是設定檔路徑寫錯，或這個終端機沒有 `export`。修好之後再跑一次 `./waf copter`。

把映像和燒錄工具複製到 Windows。`<你>` 換成你的 Windows 帳號名稱，不知道的話在 WSL 執行 `ls /mnt/c/Users` 就看得到：

```bash
cp build/hub8735ultra-expansion/sdk_build/flash_ntz.bin /mnt/c/Users/<你>/Downloads/
cp "../ambpro2_sdk/tools/Pro2_PG_tool _v1.3.0.zip" /mnt/c/Users/<你>/Downloads/
```

燒錄工具的檔名裡有一個空白，所以要加引號。

## 6. 燒錄（在 Windows）

1. **解開燒錄工具**：在 Windows 解開 `Downloads` 裡的 `Pro2_PG_tool _v1.3.0.zip`。`uartfwburn.exe` 在解開後的子資料夾 `Pro2_PG_tool _v1.3.0\` 裡（沒有這層子資料夾的話，就在解開的資料夾裡找），在 `uartfwburn.exe` 所在的資料夾開 PowerShell。
2. **接上開發板，找出 COM 埠**：開發板上有兩個 USB-C，只有一個接到 USB 轉序列晶片 CH340。接上之後，裝置管理員的「連接埠 (COM 和 LPT)」會出現 `USB-SERIAL CH340 (COMx)`，那個就是要用的口，記下 COM 編號。接另一個口不會出現 `USB-SERIAL CH340`。
   - 換了口還是沒出現，就換一條線。
   - 驅動沒有自動裝的話，到晶片廠商 WCH（沁恒）的網站下載 CH340 驅動。
3. **關掉會佔用序列埠的程式**：所有開著這個 COM 埠的程式都要關掉，例如終端機、地面站。
4. **進燒錄模式**：按住 upgrade 鍵，短按一下 reset 鍵，再放開 upgrade 鍵。
5. **馬上燒錄**（`COMx` 換成你的編號）：

   ```powershell
   .\uartfwburn.exe -p COMx -f C:\Users\<你>\Downloads\flash_ntz.bin -b 2000000 -U
   ```

成功時：先看到 `programing done.`（工具自己的拼法），最後一行是 `download success`。燒完之後按一下 reset 開機。看到 `download success` 之前，USB 保持接著。

失敗時：

- `uart COMx open fail`：剛進燒錄模式時常見，過幾秒再執行同一個指令就好，不用重按鍵。
- `ping fail` 或 `ucfg fail`：多半是進燒錄模式之後隔太久，重做第 4、5 步。

## 7. 確認開機與 Wi-Fi

開發板開機時，HAL 很早就把這個 COM 埠交給 MAVLink（SERIAL0）：序列終端機只看得到最早的幾行開機訊息，接著就是二進位資料。開機的情形改從地面站的訊息看。

1. 地面站接開發板的 COM 埠，115200 baud。Mission Planner：右上角選 `COMx` 與 `115200`，按 Connect。
2. 連上之後按一下 reset。開發板重開，地面站的訊息（Mission Planner 的 Messages 分頁）依序出現：

   ```text
   WiFi: joining the board's default hotspot (SSID length 9)
   WiFi: up, local 192.168.x.x:14555
   ```

- 第一行是用預設熱點時的樣子。用自己的熱點時是 `WiFi: joining (SSID length N)`：N 要等於你的熱點名稱的位元組數（英數字一個算 1，中文一個字算 3），對不上就是設定檔裡多了引號或空白。
- `WiFi: up` 後面的 IP 就是開發板的位址，記下來。
- 這幾行只送給當時已經連上的地面站：地面站要在按 reset 之前接好，錯過了就再按一次 reset。也可以在熱點的裝置清單看開發板的 IP，例如 Windows 的行動熱點會列出連上的裝置。

不對的時候：

- **每 5 秒出現一次 `Config Error: Baro: unable to initialise driver`**：找不到氣壓計。檢查氣壓計的接線和 3.3 V，再按 reset 重開。
- **出現 `WiFi: no STA credentials in this image; WiFi MAVLink port disabled`**：這個映像的 Wi-Fi 是關掉的：沒有設定檔，而且疊加檔把 `WIFI_SSID_STATION` 設成了 `""` 或 undef 了它。
- **出現 `WiFi: join failed (…)`**：開發板連不上熱點。它每次失敗後等 1 秒自動重試，同樣的失敗至少相隔 30 秒才再顯示一則。檢查熱點有沒有開、名稱和密碼（預設熱點是 `ardupilot`、`ardupilot1`）；熱點最好是 2.4 GHz。

SERIAL0 與預設熱點的現況見[功能狀態](docs/feature-status.zh-TW.md)。

## 8. 連地面站

1. 電腦連上同一個熱點。地面站如果還接著 COM 埠，先斷開（Mission Planner 按 Disconnect）。
2. 開發板還沒找到地面站時，會把心跳廣播到 UDP 14550，地面站軟體聽這個埠就收得到：
   - **QGroundControl**：預設就會聽 UDP 14550，打開等它自己連上。
   - **Mission Planner**：右上角的連線方式選 UDP，按 Connect，埠填 14550。
3. Windows 第一次收 UDP 時，防火牆可能會詢問，選「私人網路」允許。如果這個熱點在 Windows 被歸為「公用網路」，就要允許「公用網路」，或在 Windows 的網路設定把這個熱點改成「私人」。

成功時：

- 地面站下載參數，約 900 多筆；
- 開發板放平時 roll、pitch 接近 0；把前端（USB 接頭的另一端）抬起約 45°，pitch 變成約 +40°。

板型另外設了 D2、D9 的電池監測與 SERIAL1（D8、D15）的 GPS。這些腳沒接東西時，在地面站的參數裡把 `BATT_MONITOR` 與 `GPS1_TYPE` 設成 0，再重開開發板。

**同一個熱點上如果有別的飛控，地面站可能連到別台。** 最簡單的做法是同一個熱點上只開這一塊開發板；地面站看得到連線來源時，確認它是第 7 步記下的 IP。

## 接下來

- 裝成 T5 那台四軸：照 [hub8735ultra-expansion 的 README](hwdef/hub8735ultra-expansion/README.zh-TW.md) 做到[你自己設的](hwdef/hub8735ultra-expansion/README.zh-TW.md#你自己設的)，再照 ArduPilot 的 [First Flight with Copter](https://ardupilot.org/copter/docs/flying-arducopter.html)。
- 每一項功能做到哪裡：[功能狀態](docs/feature-status.zh-TW.md)；每次上板的紀錄：[BOARD_TESTS.md](BOARD_TESTS.md)。
- 換成自己的感測器或腳位：[描述自己的開發板](docs/custom-boards.zh-TW.md)。
- 建置與燒錄的完整選項：[建置韌體](docs/building.zh-TW.md)、[燒錄韌體](docs/flashing.zh-TW.md)。
- 卡住了、或照做成功了，都歡迎開 issue，選「Board test report / 上板結果」範本。
