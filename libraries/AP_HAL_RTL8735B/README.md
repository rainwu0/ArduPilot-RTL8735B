# ArduPilot RTL8735B 移植

ArduPilot 在 Realtek RTL8735B 上的硬體抽象層（HAL）。**未經飛行驗證，請勿用於飛行。**

開發與驗證用的板子是 HUB 8735 ultra（板商 ideasHatch，主晶片 RTL8735B；板商資料：https://github.com/ideashatch/HUB-8735）。

本儲存庫是個人維護的非官方移植，與 ArduPilot 開發團隊無關。本平台的問題請在本儲存庫回報，不要回報到 ArduPilot 的論壇或 issue。

## 需求

- Ubuntu 22.04 或 WSL（Ubuntu 22.04）
- Realtek RTL8735B SDK，提交 `d1b6426bb69339a609df4a3b7cd0059392999ddf`（https://github.com/ambiot/ambpro2_sdk），由下方腳本取得

## 取得與建置

以下指令在本儲存庫根目錄執行：

```bash
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$PWD/../ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
./waf configure --board rtl8735b-empty
./waf copter
```

SDK 預設放在本儲存庫旁的 `../ambpro2_sdk`。放在其他位置時，執行 `Tools/scripts/rtl8735b_get_sdk.sh <SDK目錄>`，以腳本結尾印出的 export 指令設定 PATH，並設定環境變數 `AMEBAPRO2_SDK=<SDK目錄>`。

工具鏈必須是 SDK 附的 Realtek ASDK（`arm-none-eabi-gcc --version` 顯示 `Realtek ASDK`）；系統套件的 `arm-none-eabi-gcc` 不能用。映像：`build/rtl8735b-empty/sdk_build/flash_ntz.bin`。

## 自訂板

`rtl8735b-empty` 的腳位依 HUB 8735 ultra 配置：序列埠、SPI、I²C、PWM 與類比輸入都在排針上，GPIO 7、25、26 沿用板子的 D 編號（25、26 是板上的綠色、藍色 LED）。其他 RTL8735B 板子要以 `--extra-hwdef`（`undef` 後重新 `define`）覆寫腳位。

`rtl8735b-empty` 不含感測器；沒有 IMU 時開機會停在 `INS: unable to initialise driver`。感測器、安裝方向與預設參數寫成另一個 hwdef 檔，以 `--extra-hwdef` 疊加：

```bash
./waf configure --board rtl8735b-empty --extra-hwdef /path/to/myboard.dat
```

目前的限制：SPI 裝置表只有一筆 `bmi160`（SPI1，片選腳為 `HAL_RTL8735B_BMI160_CS_PIN`）；GPIO 只有邏輯腳位 7、25、26。

### 板子的應用程式

自訂板可以把 SDK 端的 C 程式編進同一份韌體。在疊加的 hwdef 加一行：

```
env RTL8735B_APP_DIR <目錄>
```

- 相對路徑以本儲存庫根目錄為基準；目錄不存在時 configure 失敗。
- 目錄中的 `.c` 檔與 SDK 原始碼一起編譯，沿用 SDK 的 include 與巨集；目錄中沒有 `.c` 檔時建置失敗。
- 應用程式要定義 `void rtl8735b_app_init(void)`。韌體在建立 ArduPilot 的 task 之後、啟動排程器之前呼叫它，可以在這裡建立自己的 FreeRTOS task。
- 應用程式只能使用 SDK，不能存取 ArduPilot 的內部。task 優先權不要高於 3：ArduPilot 主執行緒為 4，見 `Scheduler.h`。
- 沒有這一行時，韌體不含任何應用程式。

## Wi-Fi

建置時以環境變數 `RTL8735B_WIFI_CONFIG` 指向本機設定檔（`ssid=`、`password=` 兩行）；沒有設定時 Wi-Fi 不啟用。帳密只寫進建置目錄。

MAVLink 預設走 UDP：本機埠 14555，還沒有地面站時把 HEARTBEAT 廣播到子網路的 14550。改用 TCP server 時，以 `--extra-hwdef` 加上一行：

```
define HAL_RTL8735B_WIFI_TCP_PORT 5760
```

TCP 同時只服務一個地面站，地面站可連到板子的 5760 埠。

安全限制：

- 帳密以常數陣列編進韌體，拿到映像檔的人可能讀出。含帳密的映像不要分享，回報問題時也不要附上。
- Wi-Fi 上的 MAVLink（UDP）沒有任何驗證。地面站停止送封包超過 3 秒後，同網段任一主機送來的封包就會被當成新的地面站，包括 RC override；沒有地面站時，HEARTBEAT 會廣播到整個子網路。只在自己控制的專用網路使用。
- TCP 同樣沒有驗證。地面站停止送資料超過 3 秒後，同網段任一主機的新連線就會取代它。

## 燒錄

### `--upload`

建置時加上 `--upload` 與 `--upload-port`，建置完成後會以 SDK 附的 `uartfwburn`（`tools/Pro2_PG_tool _v1.3.0.zip`）燒錄 `flash_ntz.bin`：

```bash
./waf copter --upload --upload-port COM7          # WSL：經 interop 執行 Windows 版工具
./waf copter --upload --upload-port /dev/ttyUSB0  # Linux：執行 Linux 版工具
```

- 看到「請讓板子進入燒錄模式」時，依開發板的按鍵讓晶片進入燒錄模式（由重置時 PA5 的電位決定；HUB 8735 ultra 是按住 upgrade 鍵、短按 reset，再放開 upgrade）。60 秒內沒有進入會自動重試，逾時則不寫入。看到 `download success` 後重置一次即開機。
- `COMn` 需要 WSL interop（能在 WSL 執行 Windows 程式），沒有時 `--upload` 會在編譯開始前提示。開了 systemd 的 WSL 在發行版閒置關閉、又在同一個 WSL VM 內重新啟動後，interop 可能不會重新註冊：在 Windows 執行 `wsl --shutdown` 讓 WSL 重新開機，並在建置與燒錄期間保持一個 WSL 終端開著。
- `/dev/ttyXXX` 需要該裝置的讀寫權限（`dialout` 群組）；在 WSL 要先以 usbipd 把 USB 序列裝置掛進 WSL。
- 環境變數 `AP_OVERRIDE_UPLOAD_CMD` 可改用自訂指令：執行時後面接上映像路徑，有 `--upload-port` 時再接 `--port <埠>`。
- 只下載、不抹除；燒完不會自動重置。

`--upload` 尚未上板驗證，見〈功能狀態〉；下面的手動燒錄已在 Windows 上使用。

### 手動燒錄

燒錄在 Windows 端進行（WSL2 預設看不到 USB 序列埠）：

1. 把 SDK 的 `tools/Pro2_PG_tool _v1.3.0.zip`（檔名含空白）解開，裡面有 `uartfwburn.exe`。
2. 開發板的 USB 序列埠接上電腦，在裝置管理員查出 COM 埠號。
3. 映像建好之後，再讓晶片進入燒錄模式（由重置時 PA5 的電位決定，依開發板的按鍵操作）。進入燒錄模式後立刻燒錄；隔太久會出現 `ping fail` 或 `ucfg fail`，重新進入燒錄模式即可。
4. 執行：

   ```powershell
   .\uartfwburn.exe -p COM7 -f <路徑>\flash_ntz.bin -b 2000000 -U
   ```

   `-p` 換成實際的埠號，`-f` 用絕對路徑。看到 `download success` 後重置一次即開機。

燒錄期間不要拔 USB；其他會自動開啟序列埠的程式（例如地面站軟體）要先關閉或改用網路連線。SDK 另附 Linux 版 `uartfwburn.linux`，尚未實測。

## 對 ArduPilot 共用程式的修改

- `ArduCopter/takeoff_check.cpp`：本平台沒有 ESC 遙測（`HAL_WITH_ESC_TELEM` 為 0），原本的起飛前檢查整段不編譯，解鎖後馬達會停在地面怠速。改為沒有 ESC 遙測時只略過馬達轉速檢查，CPU 負載檢查照常（本 HAL 尚未提供系統負載，所以目前不會擋）；有 ESC 遙測的建置不受影響。本庫直接採用上游 PR 34070（https://github.com/ArduPilot/ardupilot/pull/34070，作者 yuiseki）的提交；到 2026-10-07 這個 PR 尚未被上游合併。
- `libraries/AP_HAL/AP_HAL_Boards.h`：`HAL_BOARD_RTL8735B` 為 100，避開上游依序分配的平台編號。
- `Tools/ardupilotwaf/boards.py`（waf 的板子登記）與 `libraries/AP_Filesystem/AP_Filesystem.h`（本平台的 newlib 沒有 `dirent.h`，在此補上定義）：加入本平台，不改變其他平台的行為。

## 功能狀態

「板上驗證」：功能已在 HUB 8735 ultra 上運作；本版本的映像尚待上板複驗。

| 項目 | 狀態 | 說明 |
|---|---|---|
| 建置系統 | 板上驗證 | |
| 排程與號誌 | 板上驗證 | 400 Hz 主迴圈的長迴圈約 6.6% |
| SPI | 板上驗證 | |
| I²C | 板上驗證 | 開機時氣壓計偶發初始化失敗，原因未明 |
| UART | 板上驗證 | 只支援 8N1 |
| Wi-Fi MAVLink（UDP） | 板上驗證 | STA 模式 |
| Wi-Fi MAVLink（TCP） | 僅建置 | 建置時選擇，與 UDP 二選一 |
| 主控台／USB MAVLink | 未實作 | |
| RC 輸入 | 僅建置 | 序列 RC（AP_RCProtocol）。地面站的 RC override 走 MAVLink，不經過 RC 輸入，已在板上使用 |
| GPS | 僅建置 | |
| PWM 輸出 | 板上驗證 | 有刷馬達 PWM 已驅動馬達；電調用的一般 PWM 未上板 |
| AnalogIn | 僅建置 | 只有原始取樣，參考電壓未確認 |
| 參數存在 Flash | 板上驗證 | 重開機與斷電後保留 |
| SD 卡 | 未實作 | |
| `--upload` 燒錄 | 僅建置 | `COMn`（Windows 版工具）與 `/dev/tty*`（Linux 版工具）都未上板；手動燒錄見〈燒錄〉 |
| 韌體 OTA 更新 | 未實作 | |
| 自訂板建置 | 僅建置 | `--extra-hwdef` |

## 授權

本儲存庫整體依 GPL-3.0 散布：ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。Realtek SDK 另有授權條款（SDK 根目錄的 `Realtek_Disclaimer-2019.pdf`），不在本儲存庫內，需自行取得。建置出的韌體含 Realtek 的預編譯函式庫；本儲存庫不提供韌體映像，Releases 與 CI 產物也不提供。散布自行建置的韌體前，請自行確認是否同時符合 GPL-3.0 與 Realtek 的條款。

本移植有幾處改寫自 ArduPilot 的 ESP32 移植（ArduPilot 9f648ccabc 的 `libraries/AP_HAL_ESP32` 與 `Tools/ardupilotwaf/esp32.py`）：`Semaphores.cpp`、`Semaphores.h`、`system.cpp`、`hwdef/scripts/rtl8735b_hwdef.py`，以及 `Tools/ardupilotwaf/rtl8735b.py` 中 hwdef 產生與 cmake 設定的部分，各檔檔頭有註明。Scheduler 分成哪些 FreeRTOS task 也依照 ESP32 移植的設計。兩者同為 GPL-3.0-or-later。
