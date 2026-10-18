# 變更紀錄

English: [CHANGELOG.en.md](CHANGELOG.en.md)

這個移植值得注意的變更都記在這裡，最新的在最上面。格式照 [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/)；版號照 [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html)，1.0.0 之前照 [Cargo 的 0.y.z 慣例](https://doc.rust-lang.org/1.90.0/cargo/reference/semver.html)：不相容的改動遞增中間那一位，其他改動遞增最後一位。

各功能在每一版的狀態（板上驗證、僅建置等），以那一版 [README](README.md) 的〈功能狀態〉為準；上板的紀錄在 [BOARD_TESTS.md](BOARD_TESTS.md)。

## [Unreleased]

## [0.1.1] - 2026-10-18

和 v0.1.0 相容：原本的疊加檔、指令與參數照舊能用。

### 修正（Fixed）

- I²C：開機時先清除匯流排，再初始化控制器；重新初始化控制器時遮罩 I²C 中斷。v0.1.0 開機偶爾出現的 `Config Error: Baro: unable to initialise driver`，在板上反覆熱重開與冷開機時沒有再出現。
- 排程：IMU 停止送樣本時（例如線鬆脫），主執行緒原本一直忙等，優先權較低的 Wi-Fi、UART、IO task 都跑不到。現在主執行緒忙等超過 20 ms 就讓出一個 tick，這些 task 還能執行。地面站收到的 MAVLink 由主迴圈產生，主迴圈卡在等 IMU 時一樣收不到。只經主機測試驗證；IMU 完全不送樣本的情況還沒上板。

### 新增（Added）

- 一個 hwdef 最多四條 I²C 匯流排（`RTL8735B_I2CBUS`，SDK 的 I2C0–I2C3 各一條）。每條有自己的 bus task、逾時次數與停用，一條被停用時其他條照常。只寫一條的疊加檔照舊能用。只經建置與主機測試，還沒上板。
- README：RC 接收機與 GPS 的接法和預設序列埠（RC 輸入在 SERIAL4，GPS 在 SERIAL3）。支援 8N1、不反相的序列 RC 協定，例如 CRSF、ELRS；不支援 SBUS。附主機測試，還沒接過實物。
- README：參數換扇區後的那次開機，如果抹除期間斷電或重置，參數會全部回到預設；建議用地面站存一份參數檔。附主機測試。

## [0.1.0] - 2026-10-18

第一個標上版號的版本：ArduPilot master `9f648ccabc`（2026-09-25）加上 RTL8735B 的 HAL。

### 新增（Added）

- RTL8735B 的 HAL：排程與號誌、SPI、I²C（傳輸逾時後重新初始化控制器、必要時清除匯流排，連續 3 次逾時才停用該匯流排）、UART、Wi-Fi MAVLink（UDP，建置時可改成 TCP）、序列 RC 輸入、GPS、PWM 輸出（一般 PWM、OneShot、OneShot125、有刷）、AnalogIn 的原始取樣、參數存在 Flash。
- 板型 `hub8735ultra`（HUB 8735 ultra）、`amb82-mini`（只建置過）、`rtl8735b-empty`。
- 自訂板的 hwdef 關鍵字：`RTL8735B_I2CBUS`、`RTL8735B_SPIBUS`、`RTL8735B_SPIDEV`、`RTL8735B_RCOUT`、`RTL8735B_GPIO`、`RTL8735B_ADC`，以 `--extra-hwdef` 疊加。
- Flash 版面 `realtek-ota`（預設）、`sdk-d1b6426`、`arduino-default`，以 `FLASH_LAYOUT` 選。
- 開發板的應用程式：`env RTL8735B_APP_DIR` 與 `rtl8735b_app_init()`。
- 建置後以 `--upload` 燒錄（還沒上板驗證）。
- 範例疊加檔 `hwdef/hub8735ultra/sensors-example.dat`：BMI160 走 SPI，BMP280、GY-271（QMC5883L）走 I²C。
- 不用開發板、不用 SDK 的主機測試 `Tools/scripts/rtl8735b_test.sh`；推到 `main` 和開 PR 時，GitHub Actions 跑同一套測試與提交慣例檢查。
- 新手教學〈從零開始〉、上板驗證紀錄 BOARD_TESTS.md，以及首頁、CONTRIBUTING、HAL README、新手教學的英文版。

### 變更（Changed）

相對於這個版號之前的公開提交：

- 同一支腳被兩個設定用到時（包括啟用中的序列埠），configure 會報錯並指出衝突的兩個來源。
- 疊加檔設了 `RTL8735B_APP_DIR`、應用程式卻沒有定義 `rtl8735b_app_init()` 時，連結失敗。

### 移除（Removed）

- 舊的 `HAL_RTL8735B_*` 腳位與匯流排 `define`（`HAL_RTL8735B_I2C1_*`、`HAL_RTL8735B_SPI1_*`、`HAL_RTL8735B_RCOUT_*` 等），改用上面的關鍵字。還在用的疊加檔建置不會報錯，設定只會失效。

[Unreleased]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/rainwu0/ArduPilot-RTL8735B/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/rainwu0/ArduPilot-RTL8735B/tree/v0.1.0
