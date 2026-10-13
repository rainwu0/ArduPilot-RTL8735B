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
./waf configure --board hub8735ultra
./waf copter
```

SDK 預設放在本儲存庫旁的 `../ambpro2_sdk`。放在其他位置時，執行 `Tools/scripts/rtl8735b_get_sdk.sh <SDK目錄>`，以腳本結尾印出的 export 指令設定 PATH，並設定環境變數 `AMEBAPRO2_SDK=<SDK目錄>`。

工具鏈必須是 SDK 附的 Realtek ASDK（`arm-none-eabi-gcc --version` 顯示 `Realtek ASDK`）；系統套件的 `arm-none-eabi-gcc` 不能用。映像：`build/hub8735ultra/sdk_build/flash_ntz.bin`。

## 板型

### HUB 8735 ultra

`--board hub8735ultra`（`hwdef/hub8735ultra/hwdef.dat`）。腳位依板商 ideashatch/HUB-8735 `870a7e08de7a` 的 `amebapro2_arduino/Arduino_package/hardware/variants/ameba_hub8735_ultra/variant.cpp`、`variant.h`；D 編號就是該檔的 `AMB_Dn`，GPIO 的邏輯編號與 D 編號相同。

| 用途 | 排針 |
|---|---|
| I²C 匯流排 1（SDK 的 I2C1） | D0（PF2，SDA）、D1（PF1，SCL） |
| SPI 匯流排 1（SDK 的 SPI1） | D18（PF5，MISO）、D19（PF6，SCLK）、D20（PF7，MOSI）、D21（PF8，SPI 裝置 `bmi160` 的 CS） |
| PWM 輸出 1–4 | D11（PF14）、D22（PF11）、D23（PF12）、D24（PF13） |
| SERIAL1（UART2，MAVLink2） | D8（PD15，TX）、D15（PD16，RX） |
| SERIAL3（UART0） | D9（PA2，TX）、D10（PA3，RX） |
| SERIAL4（UART3，RC 輸入） | D6（PE1，TX）、D5（PE2，RX） |
| 類比輸入，邏輯編號 2 | D2（PF0，ADC 通道 0） |
| GPIO | 7（D7，PD17，輸入）、25（D25，PE6，綠色 LED，輸出）、26（D26，PF9，藍色 LED，輸出） |
| 保留 | D12（PF15）：板上按鍵（`variant.h` 的 `PUSH_BTN`）；D13（PF10）：保留，本板型不設定。實測上它控制板上的白色補光燈；詳見板商公開的原理圖（ideashatch/HUB-8735 `870a7e08de7a` 的 `HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf`）；D14（PA5）：`variant.h` 標為 Sensor power on，重置時的電位也決定是否進入燒錄模式（見〈燒錄〉）；D16（PF3）、D17（PF4）：LOG UART（SERIAL0，停用） |
| 未設定 | D3（PE4）、D4（PE3）：疊加檔可改成 SDK 的 I2C2（SDA、SCL），或 SPI0 的 CS、MOSI（SPI0 的 SCLK、MISO 是 PE1、PE2，要先停用 SERIAL4） |

- I²C 一律用 SDK 的編號：`variant.h` 把 PE4／PE3 標成 I2C0、PA1／PA0 標成 I2C2，SDK 的腳位表相反（`component/mbed/targets/hal/rtl8735b/i2c_api.c`）。
- 產生器不檢查同一支腳是否被兩個關鍵字用到；改用 D3、D4 或其他腳時，要自己確認沒有和上表衝突。
- `rtl8735b-empty` 的接法和 `hub8735ultra` 相同，保留給既有的建置；映像在 `build/rtl8735b-empty/`。

### 自訂板

其他 RTL8735B 板子以 `--extra-hwdef` 覆寫：序列埠以 `undef` 後重新 `define`；匯流排、輸出與腳位用下列關鍵字，先 `undef <關鍵字>` 清空整組再重寫。

| 關鍵字 | 欄位 | `hub8735ultra` 的值 |
|---|---|---|
| `RTL8735B_I2CBUS` | 邏輯匯流排、SDK 的 I2C 編號（0–3）、SDA、SCL、時脈、`internal` 或 `external` | `1 1 PIN_F2 PIN_F1 400000 external` |
| `RTL8735B_SPIBUS` | 邏輯匯流排、SDK 的 SPI 編號（0–1）、MOSI、MISO、SCLK、預設時脈 | `1 1 PIN_F7 PIN_F5 PIN_F6 1000000` |
| `RTL8735B_SPIDEV` | 名稱、邏輯匯流排、CS、mode、低速、高速 | `bmi160 1 PIN_F8 0 1000000 10000000` |
| `RTL8735B_RCOUT` | 腳位；出現的順序就是輸出通道 1、2、… | `PIN_F14`、`PIN_F11`、`PIN_F12`、`PIN_F13` 各一行 |
| `RTL8735B_GPIO` | 邏輯編號、腳位、`in` 或 `out` | `7 PIN_D17 in`、`25 PIN_E6 out`、`26 PIN_F9 out` |
| `RTL8735B_ADC` | 邏輯編號、腳位（PF0–PF3） | `2 PIN_F0` |

I²C、SPI 匯流排與類比輸入各一個，SPI 裝置 1–4 個，PWM 1–12 路；寫錯會在建置時報錯。腳位能不能當該周邊用，由 SDK 初始化時判斷，不行就不輸出。

板型都不含感測器；沒有 IMU 時開機會停在 `INS: unable to initialise driver`。感測器、安裝方向與預設參數寫成另一個 hwdef 檔，以 `--extra-hwdef` 疊加：

```bash
./waf configure --board hub8735ultra --extra-hwdef /path/to/myboard.dat
```

### AMB82-mini

`--board amb82-mini`（`hwdef/amb82-mini/hwdef.dat`）：AMB82-mini 和 HUB 8735 ultra 是同一顆晶片，周邊腳位相同（依 Realtek Ameba-AIoT/ameba-arduino-pro2 9de560f6af 的 `Arduino_package/hardware/variants/ameba_amb82-mini/variant.cpp`）。這個板型只把 GPIO 的邏輯編號改成 AMB82-mini 的 D 編號：17（PD17，輸入）、24（PE6，綠色 LED）、23（PF9，藍色 LED）。Flash 版面同樣用預設的 `realtek-ota`。

- 狀態：僅建置，未上板。
- 四路 PWM 輸出 PF11–PF14 中，PF12、PF13 在上述檔案裡另標為 `*LOG_RX`、`*LOG_TX`，能不能當 PWM 輸出用尚未確認。

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

### Flash 版面

`flash_ntz.bin` 裡的分割表（韌體槽、ISP IQ、NN 模型區等的位址與長度）依 hwdef 的 `FLASH_LAYOUT` 決定，以 `--extra-hwdef` 疊加：

```
FLASH_LAYOUT sdk-d1b6426
```

| 版面 | fw1 | fw2 | 映像上限 | 來源 |
|---|---|---|---|---|
| `realtek-ota`（預設） | 0x060000，4 MiB | 0x520000，4 MiB | 4,194,304 bytes | Realtek Arduino 套件 OTA Mode=Enable 的 `amebapro2_partitiontable_OTA.json`（Ameba-AIoT/ameba-arduino-pro2 93d6351），與 Ameba-AIoT/ameba-rtos-pro2 eb5c090 的範例分割相同 |
| `sdk-d1b6426` | 0x080000，3.5 MiB | 0x4C0000，3.5 MiB | 3,670,016 bytes | ambpro2_sdk d1b6426 範例專案的 `amebapro2_partitiontable.json` |
| `arduino-default` | 0x060000，4 MiB | 0x520000，64 KiB | 4,194,304 bytes | 同一個 Arduino 套件 OTA Mode=Disable 的 `amebapro2_partitiontable.json` |

- 狀態：僅建置。以本版本建置的映像還沒在這個版面上開過機；先前的版本用的都是 SDK 原本的分割（`sdk-d1b6426`）。
- 預設是 `realtek-ota`：手動燒錄預設建置的 `flash_ntz.bin` 時，板子的分割表也會換成這個版面。HUB 8735 ultra（ideashatch/HUB-8735 870a7e0 發行的板商套件 4.1.1）與 AMB82-mini（Ameba-AIoT/ameba-arduino-pro2 93d6351）的 Arduino 套件在 OTA Mode=Enable 時，以及新版 SDK（Ameba-AIoT/ameba-rtos-pro2 eb5c090），都用這個版面。要沿用 SDK `d1b6426` 原本的分割，選 `sdk-d1b6426`。
- 建置時以 SDK 的分割表為底，只改寫各記錄的位址與長度。
- 映像上限是 fw1 的長度：韌體燒在 fw1。`firmware_ntz.bin` 超過上限，或建出的 `flash_ntz.bin` 的槽記錄與版面不同時，建置失敗。
- 本儲存庫不提供 Wi-Fi 韌體更新，不會寫入 fw2；fw2 只是分割表中的一筆記錄。
- 版面在 `./waf configure` 時讀進來：改了 hwdef 或疊加檔的 `FLASH_LAYOUT` 之後要重新 configure。
- 參數存在 0xF80000–0xFA0000，三種版面都相同。Arduino 套件（93d6351）與新版 SDK（eb5c090）的 NOR 檔案系統區（0xF64000–0x1000000）涵蓋這一段，只在啟用 Flash 檔案系統時會衝突；本移植沒有啟用。

## 馬達輸出

| `MOT_PWM_TYPE` | 輸出 |
|---|---|
| 0 Normal、8 PWMRange、9 PWMAngle | `RC_SPEED` 50–490 Hz，脈寬 400–2100 µs |
| 1 OneShot | 同上的連續 PWM（晶片沒有單發觸發） |
| 2 OneShot125 | 脈寬 125–250 µs（1000–2000 除以 8），每階 0.25 µs；固定 1 kHz 連續輸出，不看 `RC_SPEED` |
| 3 Brushed | `RC_SPEED` 10–32 kHz |

- 有刷與 OneShot125 共用 PWM 的時脈（有刷用 40 MHz，OneShot125 用晶片內部的 4 MHz 振盪器），同一次開機只能用其中一種；另一種停止輸出，並在主控台印出原因。
- 不支援：DShot（4–7），以及 LED 輸出（NeoPixel、ProfiLED）。DShot 需要以 DMA 驅動的 PWM 通道：SDK d1b6426 的 `component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_gdma.h:69-72` 只有 PWM0–PWM3 的 GDMA handshake，而 HUB 8735 ultra 的四路輸出 PF11–PF14 是 PWM5–PWM8（`component/mbed/targets/hal/rtl8735b/pwmout_api.c:77-78` 的腳位表）。
- OneShot、OneShot125 未上板。OneShot125 上板前未確認的事：
  - 時脈來自晶片內部的 RC 振盪器，精度與溫度漂移都未量測；切到 4 MHz 後時脈是否立即可用。
  - 週期與脈寬的暫存器寫法照 Realtek 公開原始碼的有刷寫法推得（Ameba-AIoT/ameba-rtos-pro2 eb5c090 的 `component/soc/8735b/fwlib/rtl8735b/source/ram/hal_pwm.c:333-380`），實際脈寬未量測。
  - 脈寬為 0 時輸出持續低電位。
  - ESC 是否接受 1 kHz 的連續 OneShot125。

  裝上 ESC 後先做 ESC 校準。

## Wi-Fi

建置時以環境變數 `RTL8735B_WIFI_CONFIG` 指向本機設定檔（`ssid=`、`password=` 兩行）；沒有設定時 Wi-Fi 不啟用。帳密只寫進建置目錄。

MAVLink 預設走 UDP：本機埠 14555，還沒有地面站時把 HEARTBEAT 廣播到子網路的 14550。改用 TCP server 時，以 `--extra-hwdef` 加上一行：

```
define HAL_RTL8735B_WIFI_TCP_PORT 5760
```

TCP 同時只服務一個地面站，地面站可連到板子的 5760 埠。

安全限制：

- 帳密以常數陣列編進韌體，拿到映像檔的人可能讀出。含帳密的映像不要分享，回報問題時也不要附上。
- Wi-Fi 上的 MAVLink（UDP）沒有任何驗證。還沒有地面站、或地面站停止送封包超過 3 秒時，送封包到本機埠的任何來源（包括同網段的其他飛控或程式）都會被當成新的地面站，它送來的 RC override 也會被接受；沒有地面站時，HEARTBEAT 會廣播到整個子網路。只在自己控制的專用網路使用。
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
| I²C | 板上驗證 | 開機時氣壓計偶發初始化失敗，原因未明。傳輸逾時後重新初始化控制器、必要時清除匯流排，連續 3 次逾時才停用該匯流排（未上板） |
| UART | 板上驗證 | 只支援 8N1 |
| Wi-Fi MAVLink（UDP） | 板上驗證 | STA 模式 |
| Wi-Fi MAVLink（TCP） | 僅建置 | 建置時選擇，與 UDP 二選一 |
| 主控台／USB MAVLink | 未實作 | |
| RC 輸入 | 僅建置 | 序列 RC（AP_RCProtocol）。地面站的 RC override 走 MAVLink，不經過 RC 輸入，已在板上使用 |
| GPS | 僅建置 | |
| PWM 輸出 | 板上驗證 | 有刷馬達 PWM 已驅動馬達；電調用的一般 PWM、OneShot、OneShot125 未上板，見〈馬達輸出〉 |
| AnalogIn | 僅建置 | 只有原始取樣，參考電壓未確認 |
| 參數存在 Flash | 板上驗證 | 重開機與斷電後保留 |
| SD 卡 | 未實作 | |
| `--upload` 燒錄 | 僅建置 | `COMn`（Windows 版工具）與 `/dev/tty*`（Linux 版工具）都未上板；手動燒錄見〈燒錄〉 |
| 韌體 OTA 更新 | 不提供 | 本儲存庫不提供 Wi-Fi 韌體更新；更新韌體以燒錄進行，見〈燒錄〉 |
| 自訂板建置 | 僅建置 | `--extra-hwdef` |
| Flash 版面 | 僅建置 | `FLASH_LAYOUT`，預設 `realtek-ota`，見〈Flash 版面〉 |

## 授權

本儲存庫整體依 GPL-3.0 散布：ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。Realtek SDK 另有授權條款（SDK 根目錄的 `Realtek_Disclaimer-2019.pdf`），不在本儲存庫內，需自行取得。建置出的韌體含 Realtek 的預編譯函式庫；本儲存庫不提供韌體映像，Releases 與 CI 產物也不提供。散布自行建置的韌體前，請自行確認是否同時符合 GPL-3.0 與 Realtek 的條款。

本移植有幾處改寫自 ArduPilot 的 ESP32 移植（ArduPilot 9f648ccabc 的 `libraries/AP_HAL_ESP32` 與 `Tools/ardupilotwaf/esp32.py`）：`Semaphores.cpp`、`Semaphores.h`、`system.cpp`、`hwdef/scripts/rtl8735b_hwdef.py`，以及 `Tools/ardupilotwaf/rtl8735b.py` 中 hwdef 產生與 cmake 設定的部分，各檔檔頭有註明。Scheduler 分成哪些 FreeRTOS task 也依照 ESP32 移植的設計。兩者同為 GPL-3.0-or-later。
