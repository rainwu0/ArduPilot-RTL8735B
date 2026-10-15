# 從零開始：在 HUB 8735 ultra 上跑 ArduPilot

照這一頁從頭做一次，最後會在地面站上看到開發板的姿態。每一步最後都寫了「成功時會看到」。對不上就先停下來，開 issue 說卡在哪一步。

**還沒飛過，請勿用於飛行。** 這一頁只做到地面站連得上，全程不要接馬達、電調和電池。

## 你需要

- **開發板**：一塊 HUB 8735 ultra，加一條**能傳資料**的 USB-C 線。只能充電的線接上之後，電腦不會出現序列埠。
- **感測器模組**：最少兩顆，GY-BMI160（IMU）和 BMP280（氣壓計）。羅盤 GY-271（QMC5883L）可以之後再接。另外準備杜邦線或焊接工具。
- **電腦**：一台 Windows，裝好 WSL 的 Ubuntu 22.04。建置在 WSL 裡做，燒錄和地面站在 Windows 那邊。
- **Wi-Fi 熱點**：電腦和開發板都連同一個，建議用 2.4 GHz。5 GHz 能不能用還沒驗過。要用自己控制的熱點，因為 Wi-Fi 上的 MAVLink 沒有任何驗證。
- **磁碟和時間**：至少 7 GB 磁碟。從 clone 到建好映像約 13 分鐘：clone 約 3.5 分鐘、裝工具與 SDK 約 5 分鐘、建置約 3.3 分鐘（依網路與電腦而定）。

**為什麼一定要接感測器：**
- 什麼都不接的話，開機會停在 `Config Error: Baro: unable to initialise driver`。Wi-Fi 會連上熱點，但地面站收不到任何東西。這不是 Wi-Fi 壞了，而是 ArduPilot 找不到氣壓計就不往下走。
- 只接氣壓計的話，接著會停在 IMU 的 Config Error：地面站應該連得上、會收到這則錯誤，但沒有姿態。這是讀程式的推論，還沒上板驗過。
- 所以最少要接氣壓計和 IMU。

## 1. 接線

照 HAL README 的[〈接上感測器〉](README.md#接上感測器)接。

- 模組一律接 3.3 V 和 GND，不要接 5 V：RTL8735B 的 I/O 是 3.3 V。
- 3.3 V 與 GND：照開發板排針旁絲印標的 3.3 V 和 GND 接。不確定的話，看板商公開的原理圖：ideashatch/HUB-8735 `870a7e08de7a` 的 `HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf`。
- IMU 照範例擺：元件面朝上，前方是 USB 接頭的另一端。擺法不同的話，最後一步的姿態方向會不對，見〈接上感測器〉的「安裝方向」。

## 2. 在 WSL 裡取得原始碼

開 WSL 的 Ubuntu 終端機，**在 WSL 自己的家目錄裡 clone**：

```bash
cd ~
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
```

- 不要 clone 到 `/mnt/c` 底下，也不要用 Windows 的 Git：前者建置會很慢，後者會把換行改掉，腳本會壞。
- 不要加 `--recurse-submodules`：ArduPilot 的子模組很多，用不到。下一步只裝三個，建置時會再自己下載 `gtest`、`littlefs` 兩個，所以建置時也要連網。

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

成功時：`arm-none-eabi-gcc --version` 的第一行有 `Realtek ASDK`。沒有的話，就是 PATH 沒設好，或用到了系統套件的編譯器，那個不能用。PATH 沒設時，下一步的 configure 會印出 `Could not find the program ['arm-none-eabi-ar']`。

## 4. Wi-Fi 設定檔

在 WSL 的家目錄建一個檔案，例如 `~/wifi.txt`，內容兩行：

```
ssid=你的熱點名稱
password=你的熱點密碼
```

- 等號後面的內容會原樣使用。**不要加引號，等號前後和行尾也不要多打空白**，不然引號和空白會變成名稱或密碼的一部分。名稱中間的空白沒關係。
- 打錯引號或空白時，建置**不會**有任何警告。要到第 7 步看開機訊息的 `SSID length N` 才發現得了：N 是熱點名稱的位元組數，英數字一個算 1，中文一個字算 3。
- 密碼要 8–63 個字元，或 64 個十六進位字元；開放網路就把 `password=` 後面留空。長度不對時建置會失敗並說明原因。
- 這個檔案不要放進任何 git 儲存庫，可以 `chmod 600 ~/wifi.txt`。

在建置之前告訴建置系統它在哪裡：

```bash
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

設定檔在建置時才讀。改了內容，直接重新建置就好，不用重新 configure。

帳密會編進韌體：建出來的映像不要分享，回報問題時也不要附上。

**之後每次開新的終端機，先做這四行再建置：**

```bash
cd ~/ArduPilot-RTL8735B
source ~/.venv-rtl8735b/bin/activate
export PATH="$HOME/ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
export RTL8735B_WIFI_CONFIG=~/wifi.txt
```

漏了最後一行，建出來的映像會沒有 Wi-Fi。

## 5. 建置

```bash
./waf configure --board hub8735ultra --extra-hwdef libraries/AP_HAL_RTL8735B/hwdef/hub8735ultra/sensors-example.dat
./waf copter
```

成功時：

- 開頭附近有 `Wi-Fi STA: 帳密取自 <你的設定檔路徑>`；
- 最後一行是 `'copter' finished successfully`，映像在 `build/hub8735ultra/sdk_build/flash_ntz.bin`。

中途會捲過很多 `warning:`，還有 `sed: can't read ...`、`JSON header - entry : fail` 這幾行。這些都來自 SDK，不影響結果，以最後一行為準。

**如果看到的是 `Wi-Fi STA: 未設定`**：建置雖然成功了，但映像裡沒有 Wi-Fi。通常是設定檔路徑寫錯，或這個終端機沒有 `export`。修好之後再跑一次 `./waf copter`。

把映像和燒錄工具複製到 Windows。`<你>` 換成你的 Windows 帳號名稱，不知道的話在 WSL 執行 `ls /mnt/c/Users` 就看得到：

```bash
cp build/hub8735ultra/sdk_build/flash_ntz.bin /mnt/c/Users/<你>/Downloads/
cp "../ambpro2_sdk/tools/Pro2_PG_tool _v1.3.0.zip" /mnt/c/Users/<你>/Downloads/
```

燒錄工具的檔名裡有一個空白，所以要加引號。

## 6. 燒錄（在 Windows）

1. **解開燒錄工具**：在 Windows 解開 `Downloads` 裡的 `Pro2_PG_tool _v1.3.0.zip`。`uartfwburn.exe` 在解開後的子資料夾 `Pro2_PG_tool _v1.3.0\` 裡，在那個子資料夾開 PowerShell。
2. **接上開發板，找出 COM 埠**：開發板上有兩個 USB-C，只有一個接到 USB 轉序列晶片 CH340。接上之後，裝置管理員的「連接埠 (COM 和 LPT)」會出現 `USB-SERIAL CH340 (COMx)`，那個就是要用的口，記下 COM 編號。接另一個口不會出現 `USB-SERIAL CH340`。
   - 換了口還是沒出現，就換一條線。
   - 驅動沒有自動裝的話，到晶片廠商 WCH（沁恒）的網站下載 CH340 驅動。
3. **關掉會佔用序列埠的程式**：所有開著這個 COM 埠的程式都要關掉，例如終端機、地面站。
4. **進燒錄模式**：按住 upgrade 鍵，短按一下 reset 鍵，再放開 upgrade 鍵。
5. **馬上燒錄**（`COMx` 換成你的編號）：

   ```powershell
   .\uartfwburn.exe -p COMx -f C:\Users\<你>\Downloads\flash_ntz.bin -b 2000000 -U
   ```

成功時：先看到 `programing done.`（工具自己的拼法），最後一行是 `download success`。燒完之後按一下 reset 開機。

失敗時：
- `uart COMx open fail`：剛進燒錄模式時常見，過幾秒再執行同一個指令就好，不用重按鍵。
- `ping fail` 或 `ucfg fail`：多半是進燒錄模式之後隔太久，重做第 4、5 步。

燒錄期間不要拔 USB。

## 7. 看開機訊息

1. 用任一個序列終端機程式（例如 PuTTY、Tera Term）開**同一個 COM 埠**，設定 115200、8N1。
2. **先開好終端機，再按 reset**，不然會漏掉開機訊息。

成功時大約會依序看到：

```
== RAM Start ==
Storage: flash id ..., using 0xf80000-0xfa0000
WIFI initialized
WiFi: joining (SSID length N)
WiFi: up, local 192.168.x.x:14555
```

- `SSID length N` 的 N 要等於你的熱點名稱的位元組數（英數字一個算 1，中文一個字算 3）。對不上，就是設定檔裡多了引號或空白。
- `WiFi: up` 大約在開機後 5 秒出現。它後面的 IP 就是開發板的位址，記下來。
- 有接羅盤的話，約 17 秒時會多一行 `QMC5883L found on bus 1 ...`；沒接羅盤就不會出現。
- **回報問題要貼開機訊息時，先遮掉 `set ssid [...]` 那一行**：SDK 會在那裡印出你的熱點名稱。

不對的時候：
- **每 5 秒重印一次 `Config Error: Baro: unable to initialise driver`**：先按 reset 重開一次。開機時氣壓計偶爾會初始化失敗，原因還沒查到；重開還是一樣的話，檢查氣壓計的接線和 3.3 V。
- **出現 `WiFi: no STA credentials`**：映像裡沒有 Wi-Fi 帳密，回第 5 步確認有 `帳密取自`。
- **一直沒有 `WiFi: up`**：檢查熱點名稱和密碼；熱點最好是 2.4 GHz。

## 8. 連地面站

1. 電腦連上同一個熱點。序列埠的終端機可以繼續開著，不影響 Wi-Fi。
2. 開發板還沒找到地面站時，會把心跳廣播到 UDP 14550，地面站軟體聽這個埠就收得到：
   - **QGroundControl**：預設就會聽 UDP 14550，打開等它自己連上。
   - **Mission Planner**：右上角的連線方式選 UDP，按 Connect，埠填 14550。
3. Windows 第一次收 UDP 時，防火牆可能會詢問，選「私人網路」允許。

（這兩個地面站的操作步驟，還沒照這一頁實測過。）

成功時：

- 序列埠多出一行 `WiFi: ground station <電腦的 IP>:14550`；
- 地面站下載參數，約 900 多筆；
- 開發板放平時 roll、pitch 接近 0；把前端（USB 接頭的另一端）抬起約 45°，pitch 變成約 +40°。

**同一個熱點上如果有別的飛控，地面站可能連到別台。** 確認心跳的來源是第 7 步記下的 IP。

## 接下來

- 每一項功能做到哪裡：[〈功能狀態〉](README.md#功能狀態)；每次上板的紀錄：[BOARD_TESTS.md](BOARD_TESTS.md)。
- 想換成自己的感測器或腳位：[〈自訂板〉](README.md#自訂板)。
- 卡住了、或照做成功了，都歡迎開 issue，選「上板結果」範本。
