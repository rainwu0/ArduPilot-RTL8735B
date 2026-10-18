# ArduPilot RTL8735B 移植

English: [README.en.md](README.en.md)

ArduPilot 在 Realtek RTL8735B 上的硬體抽象層（HAL）。**未經飛行驗證，請勿用於飛行。**

開發與驗證用的開發板是 HUB 8735 ultra（板商 ideasHatch，主晶片 RTL8735B；板商資料：https://github.com/ideashatch/HUB-8735）。

第一次上手：照[從零開始](GETTING_STARTED.md)做一次。各功能目前能用到哪裡，見〈功能狀態〉。

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
| SERIAL3（UART0，GPS） | D9（PA2，TX）、D10（PA3，RX） |
| SERIAL4（UART3，RC 輸入） | D6（PE1，TX）、D5（PE2，RX） |
| 類比輸入，邏輯編號 2 | D2（PF0，ADC 通道 0） |
| GPIO | 7（D7，PD17，輸入）、25（D25，PE6，綠色 LED，輸出）、26（D26，PF9，藍色 LED，輸出） |
| 保留 | D12、D13、D14、D16、D17，見下方 |
| 未設定 | D3（PE4）、D4（PE3）：疊加檔可改成 SDK 的 I2C2（SDA、SCL），或 SPI0 的 CS、MOSI（SPI0 的 SCLK、MISO 是 PE1、PE2，要先停用 SERIAL4） |

保留的腳：

- D12（PF15）：板上按鍵（`variant.h` 的 `PUSH_BTN`）。
- D13（PF10）：本板型不設定。實測上它控制板上的白色補光燈；詳見板商公開的原理圖（ideashatch/HUB-8735 `870a7e08de7a` 的 `HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf`）。
- D14（PA5）：`variant.h` 標為 Sensor power on；重置時的電位也決定是否進入燒錄模式（見〈燒錄〉）。
- D16（PF3）、D17（PF4）：LOG UART，即 SERIAL0，見下方〈主控台 MAVLink〉。

主控台 MAVLink（只有建置與主機測試，沒有上板）：

- SERIAL0 是 LOG UART，經板上的 USB 轉 UART 接到開發板的 USB 序列埠，也就是燒錄用的那個埠。`SERIAL0_PROTOCOL` 預設 2（MAVLink2），`SERIAL0_BAUD` 預設 115，地面站接這個 COM 埠、115200 baud 就能連線，不需要 Wi-Fi。
- SDK 的開機訊息與 HAL 的文字訊息也從這個埠印出，會夾在 MAVLink 封包之間；地面站會重新同步，但可能掉幾個封包。保持 115200 時，這些文字用一般的序列埠終端機仍讀得到。
- ArduPilot 開始用 SERIAL0 之後，這個埠收到的位元組只交給 MAVLink，SDK 主控台不再收到指令。
- 燒錄也用這個埠：燒錄前先讓地面站斷開它。
- 不要 SERIAL0 時，在疊加檔寫 `undef HAL_RTL8735B_SERIAL0_ENABLED`，再 `define HAL_RTL8735B_SERIAL0_ENABLED 0`。
- 晶片自己的 USB（開發板另一個 USB-C）不做 MAVLink：SDK 公開的原始碼裡，USB 裝置端只有標頭，沒有實作。

RC 接收機與 GPS（只有建置與主機測試，沒有接過實物）：

- RC 接收機接 SERIAL4：接收機的 TX 接 D5（RX），RX 接 D6（TX）。`SERIAL4_PROTOCOL` 預設就是 23（RC 輸入），不用改參數。
- 支援 8N1、不反相的序列 RC 協定，例如 CRSF／ELRS（416666 baud）與 115200 baud 的協定。SBUS 要 8E2 與反相，本 HAL 的序列埠只做 8N1、不反相，所以不支援；ArduPilot 輪流試各種格式時，試到不支援的格式就先關閉序列埠，再試下一種。
- GPS 接 SERIAL3：GPS 的 TX 接 D10（RX），RX 接 D9（TX）。`SERIAL3_PROTOCOL` 預設就是 5（GPS），鮑率由 ArduPilot 自動找。接在 SERIAL1（D8 TX、D15 RX）時，要把 `SERIAL1_PROTOCOL` 改成 5，它的預設是 MAVLink2。
- 準位：RTL8735B 的 I/O 是 3.3 V，腳能不能承受 5 V 不知道。5 V 準位的接收機或 GPS 要加準位轉換。
- 主機測試涵蓋 ArduPilot 輪流試 RC 格式、GPS 自動換鮑率時序列埠的行為（`tests/hal/uart_driver_host_test.cpp`）。

其他說明：

- I²C 一律用 SDK 的編號：`variant.h` 把 PE4／PE3 標成 I2C0、PA1／PA0 標成 I2C2，SDK 的腳位表相反（`component/mbed/targets/hal/rtl8735b/i2c_api.c`）。
- 同一支腳被兩個設定用到（包括啟用中的序列埠）時，建置會報錯並指出衝突的兩個來源；改用 D3、D4 或其他腳時，先把原本用到它的設定拿掉。
- `rtl8735b-empty` 的接法和 `hub8735ultra` 相同，保留給既有的建置；映像在 `build/rtl8735b-empty/`。

#### 接上感測器

板型本身不含感測器，沒有氣壓計時開機會停在 Config Error。下面是在 HUB 8735 ultra 上接 IMU、氣壓計、羅盤的一種接法，已在板上驗證：開機後三個感測器都被偵測到（燒錄後第一次開機曾出現一次氣壓計初始化失敗，見〈功能狀態〉的 I²C 列），Wi-Fi 地面站收得到心跳與參數，參數在重開機後保留，抬起開發板前、後端時姿態方向正確（[BOARD_TESTS.md](BOARD_TESTS.md) 的 T2）。

| 感測器 | 模組 | 接到 |
|---|---|---|
| IMU | GY-BMI160 | SPI 匯流排 1：D18（MISO，接模組的 SAO／SDO）、D19（SCLK，接 SCL）、D20（MOSI，接 SDA）、D21（CS） |
| 氣壓計 | BMP280 | I²C 匯流排 1：D0（SDA）、D1（SCL）；SDO 接 GND（位址 0x76），CSB 接 3.3 V（選 I²C 模式） |
| 羅盤 | GY-271（QMC5883L） | I²C 匯流排 1：和 BMP280 共用 D0、D1 |

- 三個模組都接 3.3 V 與 GND：RTL8735B 的 I/O 是 3.3 V，訊號電位要一致。3.3 V 與 GND 照開發板排針旁絲印標的接；不確定的話，對照板商公開的原理圖（ideashatch/HUB-8735 `870a7e08de7a` 的 `HUB 8735 ultra toolkit/Hardware/HUB 8735 ultra Schematic.pdf`）。
- 模組上的腳名依廠商不同，以手上模組的絲印為準。

```bash
./waf configure --board hub8735ultra --extra-hwdef libraries/AP_HAL_RTL8735B/hwdef/hub8735ultra/sensors-example.dat
./waf copter
```

疊加檔是 `hwdef/hub8735ultra/sensors-example.dat`，它不是板型，`./waf list_boards` 不會多出板型。換成別的感測器時，改 `IMU`、`BARO`、`COMPASS` 三行就好，寫法和 ArduPilot 其他平台的 hwdef 相同，例如 ArduPilot `9f648ccabc` 的 `libraries/AP_HAL_ESP32/hwdef/esp32diy/hwdef.dat`。

安裝方向：

- IMU 的 `ROTATION_*` 要實測，不能只看模組的絲印。實測上，元件面朝上的 GY-BMI160 配 `ROTATION_NONE` 時，水平放著的開發板被當成上下顛倒。疊加檔裡的值是範例開發板實測的結果：元件面朝上，前方是 USB 接頭的另一端。
- 換了擺法要重測：開發板前端抬起時 pitch 要變正，水平放著時 roll、pitch 接近 0。
- 整片開發板在機身上轉了方向時，不改疊加檔，改參數 `AHRS_ORIENTATION`。外接羅盤的方向用參數 `COMPASS_ORIENT`。

### AMB82-mini

`--board amb82-mini`（`hwdef/amb82-mini/hwdef.dat`）：AMB82-mini 和 HUB 8735 ultra 是同一顆晶片，周邊腳位相同（依 Realtek Ameba-AIoT/ameba-arduino-pro2 9de560f6af 的 `Arduino_package/hardware/variants/ameba_amb82-mini/variant.cpp`）。這個板型只把 GPIO 的邏輯編號改成 AMB82-mini 的 D 編號：17（PD17，輸入）、24（PE6，綠色 LED）、23（PF9，藍色 LED）。Flash 版面同樣用預設的 `realtek-ota`。

- 狀態：僅建置，未上板。
- 四路 PWM 輸出 PF11–PF14 中，PF12、PF13 在上述檔案裡另標為 `*LOG_RX`、`*LOG_TX`，能不能當 PWM 輸出用尚未確認。

### 自訂板

其他 RTL8735B 開發板以 `--extra-hwdef` 覆寫：序列埠以 `undef` 後重新 `define`；匯流排、輸出與腳位用下列關鍵字：每一行加一筆；要換掉整組時，先 `undef <關鍵字>` 清空再重寫。

| 關鍵字 | 欄位 | `hub8735ultra` 的值 |
|---|---|---|
| `RTL8735B_I2CBUS` | 邏輯匯流排、SDK 的 I2C 編號（0–3）、SDA、SCL、時脈、`internal` 或 `external` | `1 1 PIN_F2 PIN_F1 400000 external` |
| `RTL8735B_SPIBUS` | 邏輯匯流排、SDK 的 SPI 編號（0–1）、MOSI、MISO、SCLK、預設時脈 | `1 1 PIN_F7 PIN_F5 PIN_F6 1000000` |
| `RTL8735B_SPIDEV` | 名稱、邏輯匯流排、CS、mode、低速、高速 | `bmi160 1 PIN_F8 0 1000000 10000000` |
| `RTL8735B_RCOUT` | 腳位；出現的順序就是輸出通道 1、2、… | `PIN_F14`、`PIN_F11`、`PIN_F12`、`PIN_F13` 各一行 |
| `RTL8735B_GPIO` | 邏輯編號、腳位、`in` 或 `out` | `7 PIN_D17 in`、`25 PIN_E6 out`、`26 PIN_F9 out` |
| `RTL8735B_ADC` | 邏輯編號、腳位（PF0–PF3、PA0–PA3） | `2 PIN_F0` |

I²C 匯流排 1–4 條（SDK 的 I2C0–I2C3 各一條），SPI 匯流排一條，SPI 裝置 1–4 個，PWM 1–12 路，類比輸入 1–8 支；寫錯會在建置時報錯。邏輯編號的範圍：匯流排 0–31、GPIO 0–255、類比輸入 0–253；I²C 的邏輯編號與 SDK 編號不能重複，SPI 裝置名稱不能重複。腳位能不能當該周邊用，由 SDK 初始化時判斷，不行就不輸出。

- `RTL8735B_I2CBUS` 的時脈：驅動向匯流排要求的時脈必須等於這一欄，否則取不到裝置（`I2CDevice.cpp` 的 `get_device_ptr` 回傳 `nullptr`）。驅動沒有指定時，ArduPilot 要求的是 400000。
- 多條 I²C：每條有自己的 bus task（名稱是 `APM_I2C` 加 SDK 編號）、逾時次數與封鎖，一條連續逾時被停用時，其他條照常。例如板上感測器留在匯流排 1，外接的另走 D3、D4：疊加檔加一行 `RTL8735B_I2CBUS 2 2 PIN_E4 PIN_E3 400000 external`。
- `internal` 與 `external`：ArduPilot 只在 `external` 的匯流排上探測外接的感測器（例如外接羅盤），接頭拉到板外的匯流排要標 `external`。
- 類比輸入：PF0–PF3 是 ADC 通道 0–3，PA0–PA3 是外接通道 4–7，每支腳各自取樣。PA2、PA3 也是 SERIAL3（UART0）的腳：要拿 PA2 當類比時，先把 SERIAL3 改成只收（下一項），或關掉 SERIAL3。
- 只收的序列埠：`define HAL_RTL8735B_SERIALn_TX_PIN PIN_NC`（先 `undef`）。這個埠只收不送（例如電調遙測，`SERIALn_PROTOCOL` 16），寫入的資料丟掉；TX 腳空出來給別的用途。例如 SERIAL3 只用 D10（PA3）收電調遙測、D9（PA2）量電調電流：疊加檔寫 `undef HAL_RTL8735B_SERIAL3_TX_PIN`、`define HAL_RTL8735B_SERIAL3_TX_PIN PIN_NC`、`RTL8735B_ADC 9 PIN_A2`。這時 GPS 改接 SERIAL1。
- 位址衝突：同一條匯流排上的兩個裝置不能同位址（板上的匯流排 1 已用 0x76 的 BMP280、0x0D 的 QMC5883L）；不同匯流排上可以同位址。衝突時改裝置的位址腳，或把其中一個移到另一條匯流排。
- 舊的寫法已經移除：`HAL_RTL8735B_I2C1_*`、`HAL_RTL8735B_SPI1_*`、`HAL_RTL8735B_BMI160_CS_PIN`、`HAL_RTL8735B_RCOUT_CHANNELS`、`HAL_RTL8735B_RCOUT_PIN0`–`PIN3`、`HAL_RTL8735B_GPIO_PIN7`／`PIN25`／`PIN26`、`HAL_RTL8735B_ADC_CHANNELS`、`HAL_RTL8735B_ADC_PIN0`、`HAL_RTL8735B_ADC_LOGICAL0` 這些 `define` 不再有作用。疊加檔若還在用，建置不會報錯，設定只會悄悄失效，要改成上表的關鍵字。

板型都不含感測器；沒有氣壓計時開機會停在 `Config Error: Baro: unable to initialise driver`，這時 Wi-Fi 也不會有 MAVLink（見〈Wi-Fi〉）。感測器、安裝方向與預設參數寫成另一個 hwdef 檔，以 `--extra-hwdef` 疊加：

```bash
./waf configure --board hub8735ultra --extra-hwdef /path/to/myboard.dat
```

### 開發板的應用程式

自訂板可以把 SDK 端的 C 程式編進同一份韌體。在疊加的 hwdef 加一行：

```
env RTL8735B_APP_DIR <目錄>
```

- 相對路徑以本儲存庫根目錄為基準；目錄不存在時 configure 失敗。
- 目錄中的 `.c` 檔與 SDK 原始碼一起編譯，沿用 SDK 的 include 與巨集；目錄中沒有 `.c` 檔時建置失敗。
- 應用程式要定義 `void rtl8735b_app_init(void)`。韌體在建立 ArduPilot 的 task 之後、啟動排程器之前呼叫它，可以在這裡建立自己的 FreeRTOS task。沒有定義時連結失敗（`undefined reference to rtl8735b_app_init`），不會悄悄變成什麼都不做。
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

- 狀態：預設的 `realtek-ota` 已在 HUB 8735 ultra 上以本版本的映像（`hub8735ultra` 板型）開機；`sdk-d1b6426`、`arduino-default` 僅建置。
- 預設是 `realtek-ota`：手動燒錄預設建置的 `flash_ntz.bin` 時，開發板的分割表也會換成這個版面。HUB 8735 ultra（ideashatch/HUB-8735 870a7e0 發行的板商套件 4.1.1）與 AMB82-mini（Ameba-AIoT/ameba-arduino-pro2 93d6351）的 Arduino 套件在 OTA Mode=Enable 時，以及新版 SDK（Ameba-AIoT/ameba-rtos-pro2 eb5c090），都用這個版面。要沿用 SDK `d1b6426` 原本的分割，選 `sdk-d1b6426`。
- 建置時以 SDK 的分割表為底，只改寫各記錄的位址與長度。
- 映像上限是 fw1 的長度：韌體燒在 fw1。`firmware_ntz.bin` 超過上限，或建出的 `flash_ntz.bin` 的槽記錄與版面不同時，建置失敗。
- 本儲存庫不提供 Wi-Fi 韌體更新，不會寫入 fw2；fw2 只是分割表中的一筆記錄。
- 版面在 `./waf configure` 時讀進來：改了 hwdef 或疊加檔的 `FLASH_LAYOUT` 之後要重新 configure。
- 參數存在 0xF80000–0xFA0000，三種版面都相同。Arduino 套件（93d6351）與新版 SDK（eb5c090）的 NOR 檔案系統區（0xF64000–0x1000000）涵蓋這一段，只在啟用 Flash 檔案系統時會衝突；本移植沒有啟用。
- 參數用 ArduPilot 的 `AP_FlashStorage` 存在兩個 64 KiB 扇區之間輪替。寫到一半斷電，只會丟掉最後那一筆。有一個例外：一個扇區寫滿、換到另一個之後，下一次開機會抹除寫滿的那個（16 次 4 KiB 抹除，時間沒有量過）。這段時間斷電或按 reset，再開機時參數會全部回到預設；上鎖前檢查會看到校正值遺失。換扇區不常發生，但參數請照常用地面站存檔，需要時寫回。

## 馬達輸出

| `MOT_PWM_TYPE` | 輸出 |
|---|---|
| 0 Normal、8 PWMRange、9 PWMAngle | `RC_SPEED` 50–490 Hz，脈寬 400–2100 µs |
| 1 OneShot | 同上的連續 PWM（晶片沒有單發觸發） |
| 2 OneShot125 | 脈寬 125–250 µs（1000–2000 除以 8），每階 0.25 µs；固定 1 kHz 連續輸出，不看 `RC_SPEED` |
| 3 Brushed | `RC_SPEED` 10–32 kHz |
| 4 DShot150、5 DShot300、6 DShot600 | 以 GPIO 送幀，各路同時；見下 |

- 有刷與 OneShot125 共用 PWM 的時脈（有刷用 40 MHz，OneShot125 用晶片內部的 4 MHz 振盪器），同一次開機只能用其中一種；另一種停止輸出，並在主控台印出原因。
- 不支援：DShot1200（7），以及 LED 輸出（NeoPixel、ProfiLED）。
- DShot：HUB 8735 ultra 的四路輸出 PF11–PF14 是 PWM5–PWM8（`component/mbed/targets/hal/rtl8735b/pwmout_api.c:77-78` 的腳位表），而 SDK d1b6426 只有 PWM0–PWM3 的 GDMA handshake（`component/soc/8735b/fwlib/rtl8735b/include/rtl8735b_gdma.h:69-72`），PWM 硬體沒辦法逐位元換脈寬。所以 DShot 改由 CPU 直接寫 GPIO 埠的設定與清除暫存器送幀，各路同時送（`dshot_shim.c`）。
  - 每送一幀關一次中斷：DShot150 約 107 µs、DShot300 約 53 µs、DShot600 約 27 µs。建議用 DShot300。
  - 主程式每次輸出送一幀；主迴圈超過 4 ms 沒有輸出時，由計時器補送上一幀。
  - 第一次切到 DShot 時以 CPU 的週期計數器量 CPU 頻率；量不到或不是 500 MHz 就不送 DShot，並在主控台印出原因。
  - 所有 DShot 輸出要在同一個 GPIO 埠；排針上的 PWM 腳都在埠 F。
  - 一路切到 DShot 之後，本次開機內那支腳維持 GPIO。改 `MOT_PWM_TYPE` 要重開機。
  - 開機訊息的 `RCOut:` 那一行顯示各路的輸出模式，例如 `RCOut: DS300:1-4`。
  - 支援經 `send_dshot_command` 送的 DShot 命令（例如由電調發出的嗶聲），以及 BLHeli_S 的位寬（`SERVO_DSHOT_ESC`）。
  - 不支援：雙向 DShot（轉速回讀）、3D 與反轉的通道設定（`SERVO_BLH_3DMASK`、`SERVO_BLH_RVMASK`）、經飛控刷電調韌體（BLHeli passthrough）。
- 電調遙測（TLM 線）：BLHeli_32、AM32 等電調的遙測線接到一個序列埠的 RX（只要接 RX），該埠設 `SERIALn_PROTOCOL` 16，鮑率自動用 115200；`MOT_PWM_TYPE` 要是 DShot，馬達極數設在 `SERVO_BLH_POLES`。電調的電流與電壓可以用 `BATTn_MONITOR` 9 當成電池監測，也記在 `ESC` 記錄與 MAVLink 的 `ESC_TELEMETRY` 訊息。不需要的自訂板，在疊加檔加一行 `undef HAL_SUPPORT_RCOUT_SERIAL`。
- DShot 與電調遙測尚未上板。上板前未確認的事：週期計數器存在且 CPU 是 500 MHz；邊緣的抖動在電調接受的範圍內；每幀關中斷不影響 Wi-Fi 與主迴圈。
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

TCP 同時只服務一個地面站，地面站可連到開發板的 5760 埠。

UDP 送給地面站的方式：

- 幾則完整的 MAVLink 湊進同一個資料包（最多 1,472 bytes）再送，資料包數降到幾分之一。湊不滿時最多等 10 ms，所以每則訊息最多晚約 10 ms 送出（`WiFiCoalesce.h`）。還沒有地面站時廣播的 HEARTBEAT 照舊一則一包；TCP 不受影響。
- 送出遇到 Wi-Fi 驅動的封包緩衝不足（`ENOBUFS`、`ENOMEM`）時，那一個資料包留著，下一步（約 1 ms 後）再送，從第一次失敗起最多 500 ms，到了才丟掉；其他送出失敗直接丟掉（`WiFiTxRetry.h`）。驅動的封包緩衝所有 socket 共用，影像串流等流量多時會暫時用完。
- 重試最多卡 500 ms，期間指令與參數回覆跟著排隊；送出緩衝區 16 KiB，裝得下這段時間的 MAVLink。重試滿 500 ms 仍送不出時整個資料包丟掉，裡面可能有十幾則訊息。

Copter 先初始化氣壓計，之後才建立 Wi-Fi 等 SERIALn 的 MAVLink 通道（`ArduCopter/system.cpp` 的 `init_ardupilot`）。找不到氣壓計時開機停在 Config Error：Wi-Fi 仍會連上熱點，但 UDP 與 TCP 都不會送出或接收 MAVLink。

安全限制：

- 帳密以常數陣列編進韌體，拿到映像檔的人可能讀出。含帳密的映像不要分享，回報問題時也不要附上。
- Wi-Fi 上的 MAVLink（UDP）沒有任何驗證。還沒有地面站、或地面站停止送封包超過 3 秒時，送封包到本機埠的任何來源（包括同網段的其他飛控或程式）都會被當成新的地面站，它送來的 RC override 也會被接受；沒有地面站時，HEARTBEAT 會廣播到整個子網路。只在自己控制的專用網路使用。
- TCP 同樣沒有驗證。地面站停止送資料超過 3 秒後，同網段任一主機的新連線就會取代它。

## SD 卡

板上的 microSD 卡經 FatFs 讀寫（`libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp`），地面站的 MAVLink FTP 看得到卡上的檔案。

- 板型 `rtl8735b-empty` 預設打開（hwdef 的 `define HAL_RTL8735B_SDCARD_ENABLED 1`），include 它的 `hub8735ultra`、`amb82-mini` 也一樣。沒有 SD 卡座的自訂板，在疊加檔加一行 `undef HAL_RTL8735B_SDCARD_ENABLED`。
- 卡先在電腦上格式化成 FAT32 或 exFAT。卡上沒有檔案系統時，也可以從地面站送 MAVLink 的 `MAV_CMD_STORAGE_FORMAT` 格式化，整張卡的資料會清掉。
- 開機後第一次存取檔案時掛載。沒插卡時每 3 秒最多重試一次，插卡後下一次存取就會掛上。
- 記錄：檔案系統記錄有編進韌體，但預設不寫（`LOG_BACKEND_TYPE` 預設 0）。要寫進卡，把 `LOG_BACKEND_TYPE` 設成 1，記錄放在 `/APM/LOGS`。設了之後沒插卡，解鎖前檢查會報 `Logging failed` 或 `No SD card`。
- SD 訊號固定 3.3 V，不用 1.8 V 的高速模式。

FatFs 在 `fatfs/`：R0.14b，取自 ArduPilot 的 ChibiOS 子模組（`modules/ChibiOS` 固定的提交 `9aebaf4a40` 的 `ext/fatfs`），內容與 ChaN 發行的 R0.14b 相同。`fatfs/ffconf.h` 是本平台的設定，取自 ArduPilot 的 `libraries/AP_HAL_ChibiOS/hwdef/common/ffconf.h`。

## 燒錄

### `--upload`

建置時加上 `--upload` 與 `--upload-port`，建置完成後會以 SDK 附的 `uartfwburn`（`tools/Pro2_PG_tool _v1.3.0.zip`）燒錄 `flash_ntz.bin`：

```bash
./waf copter --upload --upload-port COM7          # WSL：經 interop 執行 Windows 版工具
./waf copter --upload --upload-port /dev/ttyUSB0  # Linux：執行 Linux 版工具
```

- 看到「請讓開發板進入燒錄模式」時，依開發板的按鍵讓晶片進入燒錄模式（由重置時 PA5 的電位決定；HUB 8735 ultra 是按住 upgrade 鍵、短按 reset，再放開 upgrade）。60 秒內沒有進入會自動重試，逾時則不寫入。看到 `download success` 後重置一次即開機。
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

- `ArduCopter/takeoff_check.cpp`：沒有 ESC 遙測的建置（`HAL_WITH_ESC_TELEM` 為 0，例如疊加檔 `undef HAL_SUPPORT_RCOUT_SERIAL` 的自訂板），原本的起飛前檢查整段不編譯，解鎖後馬達會停在地面怠速。改為沒有 ESC 遙測時只略過馬達轉速檢查，CPU 負載檢查照常（本 HAL 尚未提供系統負載，所以目前不會擋）；有 ESC 遙測的建置（板型的預設）不受影響，照上游原本的檢查。本庫直接採用上游 PR 34070（https://github.com/ArduPilot/ardupilot/pull/34070，作者 yuiseki）的提交；到 2026-10-07 這個 PR 尚未被上游合併。
- `libraries/AP_HAL/AP_HAL_Boards.h`：`HAL_BOARD_RTL8735B` 為 100，避開上游依序分配的平台編號。
- `Tools/ardupilotwaf/boards.py`（waf 的板型登記）與 `libraries/AP_Filesystem/AP_Filesystem.h`（本平台的 newlib 沒有 `dirent.h`，在此補上定義）：加入本平台，不改變其他平台的行為。
- `libraries/AP_Filesystem/AP_Filesystem.cpp`、`AP_Filesystem_config.h`：加入本平台的 SD 卡後端 `AP_Filesystem_RTL8735B`（新檔），不改變其他平台的行為。

## 功能狀態

「板上驗證」：在 HUB 8735 ultra 上運作過，紀錄在 [BOARD_TESTS.md](BOARD_TESTS.md)，括號裡是紀錄編號。「待複驗」：先前有板上結果，但無法確認來自受測的這塊開發板，或沒有和目前版本對應的紀錄，要重新驗證。只用板型 `hub8735ultra`、沒接感測器時，能開機、連上 Wi-Fi，但停在氣壓計的 Config Error，所以沒有 Wi-Fi MAVLink（T1）；照〈接上感測器〉接好時，有 Wi-Fi MAVLink（T2；氣壓計偶發初始化失敗，見 I²C 列）。

| 項目 | 狀態 | 說明 |
|---|---|---|
| 建置系統 | 板上驗證（T1、T2） | |
| 排程與號誌 | 板上驗證（T2、T4） | Copter 主迴圈在板上執行（心跳、參數、姿態更新）；正常運作時 120 秒內 399.9 Hz、長迴圈 0.07%（T4）。主執行緒連續忙等 20 ms 時讓出一個 tick：拔掉 IMU 的 SCLK 時主迴圈與較低優先權的 task 照常，但讓出是否觸發沒有記錄；IMU 完全不送樣本時未上板（T4） |
| SPI | 板上驗證（T2） | BMI160 |
| I²C | 板上驗證（T2、T3） | BMP280、QMC5883L 共用匯流排 1。開機時先清匯流排：熱重開 23 次、寫入映像後的第一次開機與冷開機各 1 次，氣壓計都初始化成功，T2 記的開機偶發初始化失敗沒有再出現（T3）；T3 的映像是改成每條匯流排各自一個 task 之前的程式，之後的版本未上板。逾時處理未上板：傳輸逾時後重新初始化控制器、必要時清除匯流排，連續 3 次逾時才停用該匯流排。多條匯流排同時用（各自逾時、停用）：只建置與主機測試，未上板 |
| UART | 待複驗 | 只支援 8N1。只收的埠（TX 寫 `PIN_NC`）只建置與主機測試，未上板 |
| Wi-Fi MAVLink（UDP） | 板上驗證（T2） | STA 模式。送出重試與湊包（見〈Wi-Fi〉）僅建置與主機測試、未上板 |
| Wi-Fi MAVLink（TCP） | 僅建置 | 建置時選擇，與 UDP 二選一 |
| 主控台 MAVLink（SERIAL0） | 僅建置 | LOG UART，經開發板的 USB 序列埠，115200 baud；SDK 與 HAL 的文字訊息夾在封包之間。晶片自己的 USB 不做。見〈板型〉的〈主控台 MAVLink〉 |
| RC 輸入 | 僅建置 | 序列 RC（AP_RCProtocol），預設在 SERIAL4；支援 8N1、不反相的協定（CRSF、ELRS 等），不支援 SBUS（接法見〈板型〉）。主機測試涵蓋輪流試格式，沒有接過接收機實測。地面站的 RC override 走 MAVLink，不經過 RC 輸入；它在板上的使用待複驗 |
| GPS | 僅建置 | 預設在 SERIAL3；接 SERIAL1 要改 `SERIAL1_PROTOCOL`（接法見〈板型〉）。主機測試涵蓋自動換鮑率，沒有接過 GPS 實測 |
| PWM 輸出 | 待複驗 | 有刷馬達 PWM 先前驅動過馬達；電調用的一般 PWM、OneShot、OneShot125 未上板，見〈馬達輸出〉 |
| DShot 與電調遙測 | 僅建置與主機測試、未上板 | DShot150／300／600、遙測請求位元、DShot 命令；電調的 TLM 線遙測。見〈馬達輸出〉 |
| AnalogIn | 僅建置 | 12 位元，以滿刻度 3.3 V 線性換算成電壓，精度與零點偏移未經板上量測（零點可用 `BATT_VLT_OFFSET` 等參數修正）。只有板型 hwdef 列出的類比腳能用，hub8735ultra 是 D2；hwdef 可以列多支（PF0–PF3、PA0–PA3），各自取樣，多支同時用只建置與主機測試，PA0–PA3 的換算是否和 PF0–PF3 相同未上板確認；腳位能承受的電壓上限不明，腳上的電壓不要超過 3.3 V。電池電壓要先經分壓，分壓比設在 `BATT_VOLT_MULT` |
| 參數存在 Flash | 板上驗證（T2） | 重開機後保留；斷電後保留未驗證。換扇區後那次開機抹除期間斷電，參數會全部回預設，見〈Flash 版面〉 |
| SD 卡 | 僅建置與主機測試、未上板 | 掛載、讀寫、MAVLink FTP、檔案系統記錄都還沒在板上跑過。主機測試以 RAM 上的假卡涵蓋掛載與重試、格式化、檔案與目錄操作、寫入出錯後重掛、拔卡；用法見〈SD 卡〉 |
| `--upload` 燒錄 | 僅建置 | `COMn`（Windows 版工具）與 `/dev/tty*`（Linux 版工具）都未上板；手動燒錄見〈燒錄〉 |
| 韌體 OTA 更新 | 不提供 | 本儲存庫不提供 Wi-Fi 韌體更新；更新韌體以燒錄進行，見〈燒錄〉 |
| 自訂板建置 | 僅建置 | `--extra-hwdef` |
| Flash 版面 | 預設版面板上驗證（T1、T2） | `FLASH_LAYOUT`，預設 `realtek-ota` 已在板上開機；`sdk-d1b6426`、`arduino-default` 僅建置，見〈Flash 版面〉 |

## 授權

本儲存庫整體依 GPL-3.0 散布：ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。Realtek SDK 另有授權條款（SDK 根目錄的 `Realtek_Disclaimer-2019.pdf`），不在本儲存庫內，需自行取得。建置出的韌體含 Realtek 的預編譯函式庫；本儲存庫不提供韌體映像，Releases 與 CI 產物也不提供。散布自行建置的韌體前，請自行確認是否同時符合 GPL-3.0 與 Realtek 的條款。

本移植有幾處改寫自 ArduPilot 的 ESP32 移植（ArduPilot 9f648ccabc 的 `libraries/AP_HAL_ESP32` 與 `Tools/ardupilotwaf/esp32.py`）：`Semaphores.cpp`、`Semaphores.h`、`system.cpp`、`hwdef/scripts/rtl8735b_hwdef.py`，以及 `Tools/ardupilotwaf/rtl8735b.py` 中 hwdef 產生與 cmake 設定的部分，各檔檔頭有註明。Scheduler 分成哪些 FreeRTOS task 也依照 ESP32 移植的設計。兩者同為 GPL-3.0-or-later。

SD 卡的檔案系統後端 `libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp` 改寫自 ArduPilot 9f648ccabc 的 `AP_Filesystem_FATFS.cpp`，檔頭有註明。`fatfs/` 的 FatFs 依 ChaN 的 FatFs 授權（BSD 型，見 `fatfs/LICENSE.txt` 與各檔檔頭），可以和 GPL-3.0 一起散布。
