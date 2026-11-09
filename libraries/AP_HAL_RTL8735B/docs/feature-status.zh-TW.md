# 功能狀態

[English](feature-status.md)

HAL 各項功能的現況。各開發板的現況在各板子 README 的〈規格〉；每一版改了什麼，見 [CHANGELOG](../CHANGELOG.zh-TW.md)。

## 狀態

| 狀態 | 意思 |
|---|---|
| 能用（Tn） | 在板上或飛行中用過，結果正確；紀錄是 [BOARD_TESTS.md](../BOARD_TESTS.md) 的 Tn |
| 要補 | 有程式，還差一步才能用；冒號後面寫差的那一步 |
| 沒有 | 本儲存庫沒有 |

板型 `hub8735ultra` 沒接氣壓計時，開機停在 `Config Error: Baro: unable to initialise driver`，Wi-Fi 也沒有 MAVLink；照 HUB 8735 ultra README 的[接線圖](../hwdef/hub8735ultra/README.zh-TW.md#接線圖)接上感測器就有。HUB 8735 ultra 加擴充板裝在 3.5 吋四軸上，由地面站經 Wi-Fi 遙控飛過（T5）。

## 各功能

| 項目 | 狀態 | 說明 |
|---|---|---|
| 建置系統 | 能用（T1、T2） | 見[建置韌體](building.zh-TW.md) |
| 自訂板建置 | 能用（T2） | `--extra-hwdef`，見[描述自己的開發板](custom-boards.zh-TW.md) |
| 排程與號誌 | 能用（T2、T4、T5） | Copter 主迴圈 400 Hz。主執行緒連續忙等 20 ms 時讓出一個 tick，較低優先權的 task 照常執行 |
| IMU 完全不送樣本時的讓出 | 要補：上板驗一次 | |
| SPI | 能用（T2、T5） | BMI160 |
| I²C | 能用（T2、T3、T5） | BMP280、QMC5883L。開機時先清匯流排。有一塊板燒錄後第一次開機曾停在 `Config Error: Baro: unable to initialise driver`，按 reset 後正常開機（T2） |
| I²C 逾時處理 | 要補：上板驗一次 | 傳輸逾時後重新初始化控制器，必要時清匯流排；連續 3 次逾時才停用該匯流排 |
| 多條 I²C 匯流排同時用 | 要補：上板驗一次 | 每條匯流排各自一個 task，各自逾時、停用 |
| UART 8N1 | 要補：上板驗一次 | |
| UART 其他格式 | 要補：上板驗一次 | 無／奇／偶同位元，1 或 2 個停止位元 |
| UART 只收的埠 | 要補：上板驗一次 | TX 腳寫 `PIN_NC` |
| UART 反相、半雙工 | 沒有 | 見[設計說明](design.zh-TW.md#序列埠沒有反相) |
| Wi-Fi MAVLink（UDP） | 能用（T2、T5） | STA 模式 |
| 預設熱點（沒有設定檔） | 要補：上板驗一次 | SSID `ardupilot`、密碼 `ardupilot1`，見 [Wi-Fi](wifi.zh-TW.md#預設熱點) |
| 重試連線時的 Wi-Fi 訊息 | 要補：上板驗一次 | joining 只送一次，`WiFi: join failed` 要錯誤碼改變，或至少相隔 30 秒才再送，見 [Wi-Fi](wifi.zh-TW.md#訊息) |
| Wi-Fi 送出重試與湊包 | 要補：上板驗一次 | 見 [Wi-Fi](wifi.zh-TW.md) |
| Wi-Fi MAVLink（TCP） | 要補：上板驗一次 | 建置時選擇，與 UDP 二選一 |
| 主控台 MAVLink（SERIAL0） | 要補：上板驗一次 | LOG UART，經開發板的 USB 序列埠，115200 baud；接手後 SDK 與 HAL 的 printf 文字不送出，HAL 的執行期訊息（含 Wi-Fi 的 IP）走 STATUSTEXT。晶片自己的 USB 不做。見 HUB 8735 ultra README 的 [UART 對應](../hwdef/hub8735ultra/README.zh-TW.md#uart-對應) |
| 地面站手把遙控（MAVLink） | 能用（T5） | 地面站的手把經 MAVLink 送到開發板，不經遙控接收機（`RC_OPTIONS` 33 忽略接收機） |
| RC 輸入 | 要補：接接收機驗一次 | 序列 RC（AP_RCProtocol），預設在 SERIAL4；支援不反相的協定（CRSF、ELRS 等）；SBUS 要外接反相器（接法見 HUB 8735 ultra README 的 [RC 輸入](../hwdef/hub8735ultra/README.zh-TW.md#rc-輸入)） |
| GPS | 要補：接 GPS 驗一次 | 預設在 SERIAL3；接 SERIAL1 要改 `SERIAL1_PROTOCOL`（接法見 HUB 8735 ultra README 的 [UART 對應](../hwdef/hub8735ultra/README.zh-TW.md#uart-對應)） |
| 羅盤 | 要補：確認方向並校正 | QMC5883L（I²C）。外接羅盤要寫 `COMPASS` 列，見[描述自己的開發板](custom-boards.zh-TW.md) |
| 外接氣壓計與空速計 | 要補：上板驗一次 | ArduPilot 的外接 I²C 氣壓計搜尋與空速計編進板型，以 `BARO_PROBE_EXT`、`ARSPD_TYPE` 打開 |
| DShot300 | 能用（T5） | 4 合 1 電調，Bluejay。見[馬達輸出](motor-outputs.zh-TW.md) |
| DShot150、DShot600 | 要補：接電調驗一次 | 見[馬達輸出](motor-outputs.zh-TW.md) |
| 電調遙測 | 要補：接有遙測線的電調驗一次 | 電調的 TLM 線接序列埠的 RX，見[馬達輸出](motor-outputs.zh-TW.md) |
| PWM 輸出：有刷 | 要補：上板驗一次 | 見[馬達輸出](motor-outputs.zh-TW.md) |
| PWM 輸出：一般 PWM、OneShot、OneShot125 | 要補：接電調驗一次 | 見[馬達輸出](motor-outputs.zh-TW.md) |
| 主迴圈停住時停送輸出 | 要補：上板驗一次 | 解鎖中主迴圈停住時停送，恢復後只輸出重新寫入的值 |
| 電池電壓與電流（類比輸入） | 要補：校正 | 12 位元，滿刻度 3.3 V；晶片有 ADC 出廠校正參數時先以 SDK 的 `hal_adc_calc_cali_val` 校正讀值。電池電壓先經分壓，分壓比設在 `BATT_VOLT_MULT`；電流設在 `BATT_AMP_PERVLT`、`BATT_AMP_OFFSET`。腳上的電壓保持在 3.3 V 以下 |
| 其他類比腳 | 要補：上板驗一次 | 只有板型 hwdef 列出的類比腳能用，hub8735ultra 是 D2；hwdef 可以列多支（PF0–PF3、PA0–PA3），各自取樣 |
| 參數存在 Flash | 能用（T2、T5） | 重開機後保留。換扇區後那次開機抹除期間斷電，參數全部回預設，見 [Flash 版面](flash-layout.zh-TW.md) |
| 執行中抹除時擋解鎖 | 要補：上板驗一次 | 抹除期間一般解鎖被擋，見 [Flash 版面](flash-layout.zh-TW.md) |
| SD 卡 | 要補：接卡驗一次 | 見 [SD 卡](sd-card.zh-TW.md) |
| 手動燒錄（Windows 版工具） | 能用（T1） | 見[燒錄韌體](flashing.zh-TW.md) |
| 手動燒錄（Linux 版工具） | 要補：上板驗一次 | 見[燒錄韌體](flashing.zh-TW.md) |
| `--upload` 燒錄 | 要補：上板驗一次 | `COMn`（Windows 版工具）與 `/dev/tty*`（Linux 版工具），見[燒錄韌體](flashing.zh-TW.md) |
| 經 Wi-Fi 更新韌體 | 要補：上板驗一次 | 開發板的應用程式 `app/ota`，見[經 Wi-Fi 更新](flashing.zh-TW.md#經-wi-fi-更新) |
| 開發板的應用程式 | 要補：上板驗一次 | `env RTL8735B_APP_DIR`，見[開發板的應用程式](board-applications.zh-TW.md) |
| 開發板的應用程式擋解鎖 | 要補：上板驗一次 | `rtl8735b_app_arming_blocked`，見[開發板的應用程式](board-applications.zh-TW.md) |
| Flash 版面 `realtek-ota`（預設） | 能用（T1、T2） | 見 [Flash 版面](flash-layout.zh-TW.md) |
| Flash 版面 `sdk-d1b6426`、`arduino-default` | 要補：上板驗一次 | 見 [Flash 版面](flash-layout.zh-TW.md) |

各開發板的現況：[HUB 8735 ultra](../hwdef/hub8735ultra/README.zh-TW.md#現況)、[HUB 8735 ultra 加擴充板](../hwdef/hub8735ultra-expansion/README.zh-TW.md#現況)、[AMB82-mini](../hwdef/amb82-mini/README.zh-TW.md#現況)、[rtl8735b-empty](../hwdef/rtl8735b-empty/README.zh-TW.md#現況)。
