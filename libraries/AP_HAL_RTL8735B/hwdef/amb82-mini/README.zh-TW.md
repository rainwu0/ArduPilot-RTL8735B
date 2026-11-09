# AMB82-mini

[English](README.md)

## 簡介

AMB82-mini 是一塊 RTL8735B 開發板，Arduino 板定義在 Realtek 的 Ameba-AIoT/ameba-arduino-pro2 套件裡。依那份定義（`9de560f6af` 的 `Arduino_package/hardware/variants/ameba_amb82-mini/variant.cpp`），它和 HUB 8735 ultra 是同一顆晶片，周邊腳位相同。

板型 `amb82-mini` include [`../rtl8735b-empty/hwdef.dat`](../rtl8735b-empty/hwdef.dat)，只改板名與 GPIO 的邏輯編號，改成 AMB82-mini 的 D 編號。

這塊板子的現況見[現況](#現況)。本頁的事實來自 `hwdef.dat`、它 include 的設定與那份 variant 檔；本儲存庫沒有這塊開發板的其他文件。

## 規格

- 處理器：Realtek RTL8735B，和 HUB 8735 ultra 同一顆晶片（見上面的 variant 檔）。
- 記憶體、電源、尺寸與板上元件：這裡不涵蓋；本儲存庫沒有這塊開發板的板商文件。

### 現況

| 項目 | 狀態 |
|---|---|
| 在 AMB82-mini 上開機、Wi-Fi MAVLink、序列埠、PWM 輸出、RC 輸入、類比腳、電池監測、羅盤與燒錄 | 要補：上板驗一次 |
| PF12、PF13 當 PWM 輸出 | 要補：上板驗一次 |
| 接感測器的接線圖 | 沒有。要補：以疊加檔加上感測器，接到 AMB82-mini 上驗一次 |

同一顆晶片在 HUB 8735 ultra 上的現況，見它的[現況](../hub8735ultra/README.zh-TW.md#現況)。HAL 各功能見[功能狀態](../../docs/feature-status.zh-TW.md)。

## 購買

本儲存庫沒有 AMB82-mini 的購買來源。它的 Arduino 板定義在 [Ameba-AIoT/ameba-arduino-pro2](https://github.com/Ameba-AIoT/ameba-arduino-pro2)。

## 接腳

這塊開發板沒有接腳圖。板型用的是下列晶片腳位，和 `rtl8735b-empty` 相同；AMB82-mini 對這些腳的標示見 variant 檔。

| 用途 | 晶片腳位 |
|---|---|
| I²C 匯流排 1（SDK 的 I2C1） | PF2（SDA）、PF1（SCL） |
| SPI 匯流排 1（SDK 的 SPI1） | PF7（MOSI）、PF5（MISO）、PF6（SCLK）、PF8（`bmi160` 的 CS） |
| PWM 輸出 1–4 | PF14、PF11、PF12、PF13 |
| 類比腳 2 | PF0 |
| GPIO 17、24、23 | PD17、PE6、PF9 |
| 序列埠 | 見〈UART 對應〉 |

## 接線圖

板型沒有感測器；沒有氣壓計時，開機停在 `Config Error: Baro: unable to initialise driver`，Wi-Fi 也不會有 MAVLink。感測器以疊加檔加上，見[描述自己的開發板](../../docs/custom-boards.zh-TW.md)。狀態：見[現況](#現況)。

## UART 對應

| SERIALn | 晶片的 UART | TX | RX | 預設協定 |
|---|---|---|---|---|
| SERIAL0 | UART1（LOG UART） | PF4 | PF3 | MAVLink2，115200 baud |
| SERIAL1 | UART2 | PD15 | PD16 | MAVLink2，115200 baud |
| SERIAL2 | — | — | — | Wi-Fi 上的 MAVLink2 |
| SERIAL3 | UART0 | PA2 | PA3 | GPS |
| SERIAL4 | UART3 | PE1 | PE2 | RC 輸入 |

- SERIAL5–SERIAL9 沒有設定。
- AMB82-mini 怎麼把 LOG UART 接到 USB，這裡不涵蓋。
- 狀態：見[現況](#現況)。

## PWM 輸出

| 輸出 | 晶片腳位 |
|---|---|
| 1 | PF14 |
| 2 | PF11 |
| 3 | PF12 |
| 4 | PF13 |

- PF12、PF13 在 variant 檔裡另標為 `*LOG_RX`、`*LOG_TX`。
- 輸出方式與限制見[馬達輸出](../../docs/motor-outputs.zh-TW.md)。
- 狀態：見[現況](#現況)。

## RC 輸入

序列 RC 接收機接 SERIAL4（TX PE1、RX PE2）；`SERIAL4_PROTOCOL` 預設 23（RC 輸入）。支援的協定和 [HUB 8735 ultra 的 README](../hub8735ultra/README.zh-TW.md#rc-輸入) 相同，是同一顆晶片。狀態：見[現況](#現況)。

## Wi-Fi 模組

晶片的 Wi-Fi 在 SERIAL2 跑 MAVLink，見 [Wi-Fi](../../docs/wifi.zh-TW.md)。狀態：見[現況](#現況)。

## GPIO

| 邏輯編號 | 晶片腳位 | 方向 | 說明 |
|---|---|---|---|
| 17 | PD17 | 輸入 | D17 |
| 24 | PE6 | 輸出 | D24，綠色 LED |
| 23 | PF9 | 輸出 | D23，藍色 LED |

這三筆取代 `rtl8735b-empty` 的 GPIO（`hwdef.dat` 裡的 `undef RTL8735B_GPIO`）。

## 類比腳

- 類比腳 2 是 PF0（ADC 通道 0）。其他類比輸入用 `RTL8735B_ADC` 加，見 [hwdef 設定](../../docs/hwdef.zh-TW.md)。
- 沒有預留給 RSSI 或類比空速計的腳。
- 狀態：見[現況](#現況)。

## 電池監測

板型沒有設定電池監測。電池電壓經分壓接類比腳 2，以 `BATT_MONITOR` 3（類比電壓）、`BATT_VOLT_PIN` 2 讀取，分壓比設在 `BATT_VOLT_MULT`。狀態：見[現況](#現況)。

## 羅盤

板型沒有羅盤。外接的 I²C 羅盤要在疊加檔寫 `COMPASS` 列，見[描述自己的開發板](../../docs/custom-boards.zh-TW.md)。狀態：見[現況](#現況)。

## 韌體

```bash
./waf configure --board amb82-mini
./waf copter
```

- 映像是 `build/amb82-mini/sdk_build/flash_ntz.bin`。工具與 SDK 的安裝見[建置韌體](../../docs/building.zh-TW.md)。
- Flash 版面是預設的 `realtek-ota`，AMB82-mini 的 Arduino 套件在 OTA Mode=Enable 時也用這個版面（見 [Flash 版面](../../docs/flash-layout.zh-TW.md)）。

## 燒錄韌體

重置時 PA5 的電位決定晶片是否進入燒錄模式；AMB82-mini 上用哪些按鍵，這裡不涵蓋。燒錄指令見[燒錄韌體](../../docs/flashing.zh-TW.md)。狀態：見[現況](#現況)。

## 其他連結

- Arduino 板定義：[Ameba-AIoT/ameba-arduino-pro2](https://github.com/Ameba-AIoT/ameba-arduino-pro2) `9de560f6af` 的 `Arduino_package/hardware/variants/ameba_amb82-mini/variant.cpp`。
- [功能狀態](../../docs/feature-status.zh-TW.md)：HAL 各功能的現況。
