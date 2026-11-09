# rtl8735b-empty

[English](README.md)

## 簡介

`rtl8735b-empty` 是放本 HAL 對 RTL8735B 開發板設定的板型，不含特定開發板的名稱，也沒有感測器：匯流排、PWM 輸出、類比輸入、GPIO、序列埠、Wi-Fi 與 SD 卡。它的腳位接法和 HUB 8735 ultra 相同。

- `hub8735ultra` 與 `amb82-mini` include 它的 `hwdef.dat`，只改板名；`amb82-mini` 另外改 GPIO 編號。
- 保留給指名它的既有建置，也當疊加檔的起點；映像在 `build/rtl8735b-empty/`。
- 狀態：見[現況](#現況)。

## 規格

這是一組設定，不是一塊開發板。摘要：

- 一條 I²C 匯流排（`external`），一條 SPI 匯流排與一個裝置（`bmi160`），四路 PWM 輸出，一支類比輸入，三個 GPIO。
- SERIAL0、SERIAL1、SERIAL3、SERIAL4 用晶片的 UART，SERIAL2 是 Wi-Fi。
- microSD 卡打開（`HAL_RTL8735B_SDCARD_ENABLED 1`）；經序列埠的電調遙測打開（`HAL_SUPPORT_RCOUT_SERIAL 1`）。
- Lua 腳本關閉（`AP_SCRIPTING_ENABLED 0`）；地形關閉（`AP_TERRAIN_AVAILABLE 0`）；預設沒有記錄後端（`HAL_LOGGING_BACKENDS_DEFAULT 0`）；沒有提示 LED（`DEFAULT_NTF_LED_TYPES Notify_LED_None`）。
- Flash 版面是預設的 `realtek-ota`（[Flash 版面](../../docs/flash-layout.zh-TW.md)）。

### 現況

| 項目 | 狀態 |
|---|---|
| 直接燒錄這個板型 | 要補：上板驗一次 |

`hub8735ultra` 除了板名之外設定相同，它的現況見它的[現況](../hub8735ultra/README.zh-TW.md#現況)。HAL 各功能見[功能狀態](../../docs/feature-status.zh-TW.md)。

## 購買

不適用：這個板型不描述特定的開發板。開發板見 [HUB 8735 ultra](../hub8735ultra/README.zh-TW.md) 與 [AMB82-mini](../amb82-mini/README.zh-TW.md) 的 README。

## 接腳

沒有接腳圖：下列晶片腳位在 HUB 8735 ultra 上的標示，見它的[接腳](../hub8735ultra/README.zh-TW.md#接腳)。

| 用途 | 晶片腳位 | 設定 |
|---|---|---|
| I²C 匯流排 1（SDK 的 I2C1） | PF2（SDA）、PF1（SCL） | `RTL8735B_I2CBUS 1 1 PIN_F2 PIN_F1 400000 external` |
| SPI 匯流排 1（SDK 的 SPI1） | PF7（MOSI）、PF5（MISO）、PF6（SCLK） | `RTL8735B_SPIBUS 1 1 PIN_F7 PIN_F5 PIN_F6 1000000` |
| SPI 裝置 `bmi160` | PF8（CS） | `RTL8735B_SPIDEV bmi160 1 PIN_F8 0 1000000 10000000` |
| PWM 輸出 1–4 | PF14、PF11、PF12、PF13 | `RTL8735B_RCOUT`，各一行 |
| 類比腳 2 | PF0 | `RTL8735B_ADC 2 PIN_F0` |
| GPIO 7、25、26 | PD17、PE6、PF9 | `RTL8735B_GPIO` |
| 序列埠 | 見〈UART 對應〉 | |

## 接線圖

板型沒有感測器。沒有氣壓計時，開機停在 `Config Error: Baro: unable to initialise driver`，Wi-Fi 也不會有 MAVLink。感測器以疊加檔加上，見[描述自己的開發板](../../docs/custom-boards.zh-TW.md)；同樣腳位上驗證過的接法，見 HUB 8735 ultra 的[接線圖](../hub8735ultra/README.zh-TW.md#接線圖)。

## UART 對應

| SERIALn | 晶片的 UART | TX | RX | 預設協定 |
|---|---|---|---|---|
| SERIAL0 | UART1（LOG UART） | PF4 | PF3 | MAVLink2，115200 baud |
| SERIAL1 | UART2 | PD15 | PD16 | MAVLink2，115200 baud |
| SERIAL2 | — | — | — | Wi-Fi 上的 MAVLink2 |
| SERIAL3 | UART0 | PA2 | PA3 | GPS |
| SERIAL4 | UART3 | PE1 | PE2 | RC 輸入 |

SERIAL5–SERIAL9 沒有設定。每個埠以 `HAL_RTL8735B_SERIALn_UART_ID`、`_TX_PIN`、`_RX_PIN`、`_ENABLED` 設定，見 [hwdef 設定](../../docs/hwdef.zh-TW.md#序列埠)。

## PWM 輸出

| 輸出 | 晶片腳位 |
|---|---|
| 1 | PF14 |
| 2 | PF11 |
| 3 | PF12 |
| 4 | PF13 |

輸出方式與限制見[馬達輸出](../../docs/motor-outputs.zh-TW.md)。

## RC 輸入

SERIAL4 預設是 RC 輸入（`DEFAULT_SERIAL4_PROTOCOL SerialProtocol_RCIN`），ArduPilot 的序列 RC 協定有編進來（`AP_RCPROTOCOL_ENABLED 1`）。能用的協定見 HUB 8735 ultra 的 [RC 輸入](../hub8735ultra/README.zh-TW.md#rc-輸入)。

## Wi-Fi 模組

Wi-Fi 是 SERIAL2（`HAL_RTL8735B_WIFI_SERIAL 2`），以 UDP 跑 MAVLink2：本機埠 14555，還沒有地面站時把 HEARTBEAT 廣播到 14550 埠。見 [Wi-Fi](../../docs/wifi.zh-TW.md)。

沒有 Wi-Fi 設定檔時，開發板連這個板型設定的預設熱點：`WIFI_SSID_STATION` `"ardupilot"`、`WIFI_PWD` `"ardupilot1"`（[預設熱點](../../docs/wifi.zh-TW.md#預設熱點)）。

## GPIO

| 邏輯編號 | 晶片腳位 | 方向 |
|---|---|---|
| 7 | PD17 | 輸入 |
| 25 | PE6 | 輸出 |
| 26 | PF9 | 輸出 |

## 類比腳

- 類比腳 2 是 PF0（ADC 通道 0）。其他類比輸入用 `RTL8735B_ADC` 加，見 [hwdef 設定](../../docs/hwdef.zh-TW.md)。
- 沒有預留給 RSSI 或類比空速計的腳。空速計的預設型號是 0，不啟用（`HAL_AIRSPEED_TYPE_DEFAULT 0`）。

## 電池監測

板型沒有設定電池監測。電池電壓經分壓接類比腳 2，以 `BATT_MONITOR` 3（類比電壓）、`BATT_VOLT_PIN` 2 讀取，分壓比設在 `BATT_VOLT_MULT`。

## 羅盤

板型沒有羅盤，也不自動搜尋外接羅盤。外接的 I²C 羅盤要在疊加檔寫 `COMPASS` 列，見[描述自己的開發板](../../docs/custom-boards.zh-TW.md)。

## 韌體

```bash
./waf configure --board rtl8735b-empty
./waf copter
```

映像是 `build/rtl8735b-empty/sdk_build/flash_ntz.bin`。工具與 SDK 的安裝見[建置韌體](../../docs/building.zh-TW.md)。沒有感測器的開發板，可以用這個板型執行範例 `examples/AppHost`，見[開發板的應用程式](../../docs/board-applications.zh-TW.md)。

## 燒錄韌體

重置時 PA5 的電位決定晶片是否進入燒錄模式；按鍵在各開發板上，例如 HUB 8735 ultra 的[燒錄韌體](../hub8735ultra/README.zh-TW.md#燒錄韌體)。燒錄指令見[燒錄韌體](../../docs/flashing.zh-TW.md)。

## 其他連結

- [`hwdef.dat`](hwdef.dat)：這個板型的所有設定。
- [hwdef 設定](../../docs/hwdef.zh-TW.md)：各個 `RTL8735B_*` 關鍵字與 `HAL_RTL8735B_*` 設定的作用。
