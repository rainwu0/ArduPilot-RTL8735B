# 變更紀錄

<!-- markdownlint-configure-file { "MD024": { "siblings_only": true } } -->

[English](CHANGELOG.md)

這個移植值得注意的變更都記在這裡，最新的在最上面。格式照 [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/)；版號照 [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html)，1.0.0 之前照 [Cargo 的 0.y.z 慣例](https://doc.rust-lang.org/1.90.0/cargo/reference/semver.html)：不相容的改動遞增中間那一位，其他改動遞增最後一位。

各功能在每一版的狀態，以那一版的功能狀態為準：[docs/feature-status.zh-TW.md](docs/feature-status.zh-TW.md)，v0.2.0 以前是 HAL README 的〈功能狀態〉。上板的紀錄在 [BOARD_TESTS.md](BOARD_TESTS.md)。

## [Unreleased]

和 v0.2.0 相容：原本的疊加檔、指令與參數照舊能用；改動要重新建置、燒錄才有。

### 變更（Changed）

- **SERIAL0（LOG UART）在 HAL 初始化時就接手，之後 SDK 與 HAL 以 printf 印的文字不再送出**，不插進 MAVLink 封包之間；序列終端機只看得到最早的幾行開機訊息。HAL 的執行期訊息以 STATUSTEXT 送到地面站，Wi-Fi 的連線訊息也是：`WiFi: joining (SSID length N)`、`WiFi: up, local <IP>:<埠>`、`WiFi: join failed (…)`、`WiFi: no STA credentials in this image; WiFi MAVLink port disabled`。開發板的 IP 改從接 COM 埠的地面站看，〈從零開始〉第 7、8 步跟著改。要看當機傾印時，在疊加檔關掉 SERIAL0。
- 類比輸入套用晶片的 ADC 出廠校正：晶片有校正參數時，讀值先校正再換算成電壓；沒有參數時照舊。
- 文件的檔名：每份文件的英文版改用預設檔名，繁體中文版以 `.zh-TW.md` 結尾。`README.en.md`、`GETTING_STARTED.en.md`、`CHANGELOG.en.md`、`.github/CONTRIBUTING.en.md` 改名為 `README.md`、`GETTING_STARTED.md`、`CHANGELOG.md`、`.github/CONTRIBUTING.md`；原本用這些檔名的中文版改名為 `README.zh-TW.md`、`GETTING_STARTED.zh-TW.md`、`CHANGELOG.zh-TW.md`、`.github/CONTRIBUTING.zh-TW.md`。GitHub 首頁預設顯示英文。
- 文件的編排：HAL README 留下板型、文件索引、對 ArduPilot 共用程式的修改與授權，其他各節移到 `docs/`：`building`（取得與建置）、`flashing`（燒錄）、`custom-boards` 與 `hwdef`（自訂板）、`board-applications`、`flash-layout`、`motor-outputs`、`wifi`、`sd-card`、`feature-status`，各有英文與中文版。
- 文件：HUB 8735 ultra 的接腳、匯流排、序列埠、PWM 輸出、GPIO、類比腳、感測器接法與燒錄，從 HAL README 移到 `hwdef/hub8735ultra/README.zh-TW.md`，章節照 ArduPilot 的板子 README，並附 32 腳的接腳圖（`pinout.zh-TW.png`、`pinout.zh-TW.svg`）。
- 文件：功能狀態改成現況表，每項是能用（附上板紀錄）、要補（寫差的那一步）或沒有；首頁與各板子 README 也各有一張現況表。首頁與 HAL README 的飛行狀態照 T5 寫，〈從零開始〉不再提。這份變更紀錄的條目不再寫各功能的狀態。

### 新增（Added）

- 序列埠支援無／奇／偶同位元與 1、2 個停止位元（原本只有 8N1），改格式立即生效。SBUS（8E2）經外接反相器就能當 RC 輸入；晶片的 UART 沒有一般模式的反相，也沒有半雙工。
- 通用板編進外接 I²C 氣壓計的搜尋與空速計：以 `BARO_PROBE_EXT`、`ARSPD_TYPE` 打開，預設都關閉，開機行為不變。外接羅盤的自動搜尋不預設打開（會蓋掉疊加檔 `COMPASS` 列的方向）。
- 文件：板型 `amb82-mini`、`rtl8735b-empty` 的 README；`docs/design.zh-TW.md`，說明本移植為什麼這樣運作；`.github/SECURITY.zh-TW.md`，說明安全問題怎麼回報。
- 文件：上板紀錄 `BOARD_TESTS.md` 加 T5：HUB 8735 ultra 加擴充板裝在 3.5 吋四軸上，2026-10-25 與 2026-11-06 由地面站經 Wi-Fi MAVLink 遙控飛行，DShot300 輸出到 4 合 1 電調。

### 修正（Fixed）

- 類比輸入在第一次取樣前回 0，不再回 NaN。原本開機時第一次讀電池的電壓與電流得到 NaN，記成內部錯誤 `constraining_nan`，要重開機才能解鎖。
- IO 與 Storage 執行緒在 `setup()` 之前就開始。原本 `setup()` 裡每次存參數都等到 2 秒逾時（在沒接馬達的板上 `setup()` 約 55 秒），`setup()` 沒做完時存的參數也不會寫進 Flash。
- README：〈對 ArduPilot 共用程式的修改〉列出 `libraries/AP_HAL/board/rtl8735b.h`。

## [0.2.0] - 2026-10-22

和 v0.1.1 有兩處不相容：範例疊加檔移到範例庫，以及 PF3、PF4 預設給 SERIAL0（見下面第一、二條）。其他疊加檔、指令與參數照舊能用；這一版的新功能都要重新建置、燒錄才有。

### 移除（Removed）

- **範例疊加檔 `hwdef/hub8735ultra/sensors-example.dat` 移到範例庫。** 照 v0.1.1 的 README 或〈從零開始〉指定這個檔的建置會找不到檔；改用範例庫 [rainwu0/ArduPilot-Hub8735Ultra](https://github.com/rainwu0/ArduPilot-Hub8735Ultra) 的 `examples/bmi160-bmp280-qmc5883l/overlay.dat`，感測器設定相同。每塊板專用的設定放範例庫，HAL 庫只留板型。

### 變更（Changed）

- **SERIAL0（LOG UART，PF3、PF4）預設打開，跑 MAVLink2、115200 baud。** 疊加檔把 PF3 或 PF4 拿去當別的用途（例如 GPIO）時建置會報錯，要先關 SERIAL0（`undef HAL_RTL8735B_SERIAL0_ENABLED` 再 `define HAL_RTL8735B_SERIAL0_ENABLED 0`）。ArduPilot 開始用這個埠之後，SDK 主控台不再收指令。
- 解鎖中主迴圈超過 100 ms 沒跑完一圈時，所有 PWM 輸出停止（電調收不到訊號而停機），DShot 改送油門 0；主迴圈恢復後，只有主程式重新寫入的通道以新值輸出。
- 執行中抹除參數區時（同一次開機第二次換扇區，或寫入失敗後重建），解鎖前檢查報 `Param storage failed`，一般解鎖被擋，抹完自動恢復；強制解鎖不經這項檢查。
- Wi-Fi UDP 送給地面站時，幾則 MAVLink 湊成一個資料包再送，每則最多晚約 10 ms；驅動的封包緩衝不足時，那個資料包最多重試 500 ms。
- 執行中的警告（輸出模式、PWM 時脈、Wi-Fi 斷線等）除了印在主控台，也以 STATUSTEXT 送到地面站。
- 建置不再編譯 SDK 的 `ethernet_api.c`（GPL-2.0-only；本移植不用乙太網路，原本連結時就丟掉）。

### 新增（Added）

- 主控台 MAVLink：地面站接開發板的 USB 序列埠（燒錄用的那個埠）、115200 baud 就能連線，不需要 Wi-Fi。SDK 與 HAL 的文字訊息夾在封包之間；燒錄前先讓地面站斷開這個埠。晶片自己的 USB 不做 MAVLink。
- 類比輸入換算成電壓（12 位元，滿刻度 3.3 V），可以用類比電池監測（分壓比設在 `BATT_VOLT_MULT`）；hwdef 可以列多支類比腳（PF0–PF3、PA0–PA3），各自取樣。
- 只收的序列埠：TX 腳寫 `PIN_NC`，例如電調遙測只用 RX，TX 腳空出來當類比輸入。
- DShot150、DShot300、DShot600（`MOT_PWM_TYPE` 4–6）：由 CPU 寫 GPIO 送幀，各路同時，至少 800 Hz；支援 DShot 命令與 BLHeli_S 的位寬。不支援雙向 DShot、DShot1200、3D。
- 電調遙測：電調的 TLM 線接一個序列埠的 RX（`SERIALn_PROTOCOL` 16），電流與電壓可以當電池監測（`BATTn_MONITOR` 9），也記在 `ESC` 記錄。
- microSD 卡：經 FatFs（R0.14b）讀寫，地面站的 MAVLink FTP 看得到卡上的檔案；`LOG_BACKEND_TYPE` 設 1 時記錄寫進卡（預設不寫）。板型 `rtl8735b-empty` 與 include 它的板型預設打開，沒有卡座的自訂板在疊加檔關掉。
- 開發板的應用程式介面（`app/rtl8735b_app.h`）：`setup()` 完成後與韌體重開機前的呼叫點，查詢解鎖狀態、主迴圈時刻，讓輸出安全；應用程式也拿得到 Flash 版面的位址。沒有感測器的開發板可以用範例 `examples/AppHost` 執行應用程式。
- README：DShot 與電調遙測、主控台 MAVLink、SD 卡、多支類比腳與只收的序列埠、Wi-Fi 送出方式的說明。

### 修正（Fixed）

- `Tools/scripts/rtl8735b_get_sdk.sh` 的說明：SDK 的位置由第一個參數指定，`AMEBAPRO2_SDK` 是 waf 讀的。

## [0.1.1] - 2026-10-19

和 v0.1.0 相容：原本的疊加檔、指令與參數照舊能用。

### 修正（Fixed）

- I²C：針對 v0.1.0 開機偶爾出現的 `Config Error: Baro: unable to initialise driver`，開機時先清除匯流排，再初始化控制器；重新初始化控制器時遮罩 I²C 中斷。
- 排程：IMU 停止送樣本時（例如線鬆脫），主執行緒原本一直忙等，優先權較低的 Wi-Fi、UART、IO task 都跑不到。現在主執行緒忙等超過 20 ms 就讓出一個 tick，這些 task 還能執行。地面站收到的 MAVLink 由主迴圈產生，主迴圈卡在等 IMU 時一樣收不到。

### 新增（Added）

- 一個 hwdef 最多四條 I²C 匯流排（`RTL8735B_I2CBUS`，SDK 的 I2C0–I2C3 各一條）。每條有自己的 bus task、逾時次數與停用，一條被停用時其他條照常。只寫一條的疊加檔照舊能用。
- README：RC 接收機與 GPS 的接法和預設序列埠（RC 輸入在 SERIAL4，GPS 在 SERIAL3）。支援 8N1、不反相的序列 RC 協定，例如 CRSF、ELRS；不支援 SBUS。
- README：參數換扇區後的那次開機，如果抹除期間斷電或重置，參數會全部回到預設；建議用地面站存一份參數檔。

## [0.1.0] - 2026-10-18

第一個標上版號的版本：ArduPilot master `9f648ccabc`（2026-09-25）加上 RTL8735B 的 HAL。

### 新增（Added）

- RTL8735B 的 HAL：排程與號誌、SPI、I²C（傳輸逾時後重新初始化控制器、必要時清除匯流排，連續 3 次逾時才停用該匯流排）、UART、Wi-Fi MAVLink（UDP，建置時可改成 TCP）、序列 RC 輸入、GPS、PWM 輸出（一般 PWM、OneShot、OneShot125、有刷）、AnalogIn 的原始取樣、參數存在 Flash。
- 板型 `hub8735ultra`（HUB 8735 ultra）、`amb82-mini`、`rtl8735b-empty`。
- 自訂板的 hwdef 關鍵字：`RTL8735B_I2CBUS`、`RTL8735B_SPIBUS`、`RTL8735B_SPIDEV`、`RTL8735B_RCOUT`、`RTL8735B_GPIO`、`RTL8735B_ADC`，以 `--extra-hwdef` 疊加。
- Flash 版面 `realtek-ota`（預設）、`sdk-d1b6426`、`arduino-default`，以 `FLASH_LAYOUT` 選。
- 開發板的應用程式：`env RTL8735B_APP_DIR` 與 `rtl8735b_app_init()`。
- 建置後以 `--upload` 燒錄。
- 範例疊加檔 `hwdef/hub8735ultra/sensors-example.dat`：BMI160 走 SPI，BMP280、GY-271（QMC5883L）走 I²C。
- 不用開發板、不用 SDK 的主機測試 `Tools/scripts/rtl8735b_test.sh`；推到 `main` 和開 PR 時，GitHub Actions 跑同一套測試與提交慣例檢查。
- 新手教學〈從零開始〉、上板驗證紀錄 BOARD_TESTS.md，以及首頁、CONTRIBUTING、HAL README、新手教學的英文版。

### 變更（Changed）

相對於這個版號之前的公開提交：

- 同一支腳被兩個設定用到時（包括啟用中的序列埠），configure 會報錯並指出衝突的兩個來源。
- 疊加檔設了 `RTL8735B_APP_DIR`、應用程式卻沒有定義 `rtl8735b_app_init()` 時，連結失敗。

### 移除（Removed）

- 舊的 `HAL_RTL8735B_*` 腳位與匯流排 `define`（`HAL_RTL8735B_I2C1_*`、`HAL_RTL8735B_SPI1_*`、`HAL_RTL8735B_RCOUT_*` 等），改用上面的關鍵字。還在用的疊加檔建置不會報錯，設定只會失效。

[Unreleased]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.1...v0.2.0
[0.1.1]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/rainwu0/ArduPilot-RTL8735B/tree/v0.1.0
