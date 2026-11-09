# hwdef 設定

[English](hwdef.md)

本 HAL 從板型的 `hwdef.dat`，以及 `--extra-hwdef` 疊加的檔案讀取的設定。怎麼用在一塊開發板上，見[描述自己的開發板](custom-boards.zh-TW.md)。感測器（`IMU`、`BARO`、`COMPASS`）的寫法和 ArduPilot 其他平台的 hwdef 相同。

## 匯流排、輸出與腳位的關鍵字

每一行加一筆。`undef <關鍵字>` 清掉它之前寫的這個關鍵字的所有筆。

| 關鍵字 | 欄位 | `rtl8735b-empty` 的值 |
|---|---|---|
| `RTL8735B_I2CBUS` | 邏輯匯流排、SDK 的 I2C 編號（0–3）、SDA、SCL、時脈、`internal` 或 `external` | `1 1 PIN_F2 PIN_F1 400000 external` |
| `RTL8735B_SPIBUS` | 邏輯匯流排、SDK 的 SPI 編號（0–1）、MOSI、MISO、SCLK、預設時脈 | `1 1 PIN_F7 PIN_F5 PIN_F6 1000000` |
| `RTL8735B_SPIDEV` | 名稱、邏輯匯流排、CS、mode、低速、高速 | `bmi160 1 PIN_F8 0 1000000 10000000` |
| `RTL8735B_RCOUT` | 腳位；出現的順序就是輸出通道 1、2、… | `PIN_F14`、`PIN_F11`、`PIN_F12`、`PIN_F13` 各一行 |
| `RTL8735B_GPIO` | 邏輯編號、腳位、`in` 或 `out` | `7 PIN_D17 in`、`25 PIN_E6 out`、`26 PIN_F9 out` |
| `RTL8735B_ADC` | 邏輯編號、腳位（PF0–PF3、PA0–PA3） | `2 PIN_F0` |

建置時檢查的限制：

- I²C 匯流排 1–4 條（SDK 的 I2C0–I2C3 各一條），SPI 匯流排一條，SPI 裝置 1–4 個，PWM 1–12 路，類比輸入 1–8 支。
- 邏輯編號：匯流排 0–31、GPIO 0–255、類比輸入 0–253。I²C 的邏輯編號與 SDK 編號不重複，SPI 裝置名稱不重複。
- 同一支腳被兩個設定用到（包括啟用中的序列埠）時，建置失敗並指出衝突的兩個來源。

腳位是否能當該周邊用，由 SDK 初始化時判斷，不行就不輸出。

### I²C

- SDK 的 I2C 編號用 SDK 的編號（I2C0–I2C3），和板商 Arduino variant 的標示可能不同（[原因](design.zh-TW.md)）。
- 驅動向匯流排要求的時脈必須等於時脈欄，否則取不到裝置（`I2CDevice.cpp` 的 `get_device_ptr` 回傳 `nullptr`）。驅動沒有指定時，ArduPilot 要求的是 400000。
- 每條匯流排有自己的 bus task（名稱是 `APM_I2C` 加 SDK 編號）、逾時次數與封鎖，一條連續逾時被停用時，其他條照常。
- ArduPilot 只在 `external` 的匯流排上探測外接的感測器（例如外接氣壓計）。
- 同一條匯流排上的兩個裝置不能同位址；不同匯流排上可以。

### 類比輸入

- PF0–PF3 是 ADC 通道 0–3，PA0–PA3 是外接通道 4–7。每支腳各自取樣。
- 板型中 PA2、PA3 也是 SERIAL3（UART0）的腳。

## 序列埠

| 設定 | 意思 |
|---|---|
| `HAL_RTL8735B_SERIALn_UART_ID` | SERIALn 用晶片的哪個 UART |
| `HAL_RTL8735B_SERIALn_TX_PIN`、`HAL_RTL8735B_SERIALn_RX_PIN` | 它的腳。TX 腳寫 `PIN_NC` 時這個埠只收：寫入的資料丟掉，TX 腳空出來。SERIAL0（LOG UART）不適用：這樣設時它不會開啟 |
| `HAL_RTL8735B_SERIALn_ENABLED` | 1：開；0：關 |

板型的值以 `undef` 後重新 `define` 取代。各板型的值見各板子 README 的〈UART 對應〉。

## 其他設定

| 設定 | 作用 | 板型中 |
|---|---|---|
| `define HAL_RTL8735B_WIFI_TCP_PORT <埠>` | Wi-Fi MAVLink 改成在這個埠的 TCP server，不用 UDP，見 [Wi-Fi](wifi.zh-TW.md) | 沒有設：UDP |
| `HAL_RTL8735B_SDCARD_ENABLED` | microSD 卡，見 [SD 卡](sd-card.zh-TW.md)；`undef` 就關掉 | 1 |
| `HAL_SUPPORT_RCOUT_SERIAL` | 經序列埠的電調遙測，見[馬達輸出](motor-outputs.zh-TW.md)；`undef` 就拿掉 | 1 |
| `define AP_COMPASS_PROBING_ENABLED 1` | 開機時在 `external` 匯流排找所有編進來的羅盤型號，方向用驅動的預設值（[預設不開的原因](design.zh-TW.md#外接羅盤預設不自動搜尋)） | 沒有設 |
| `FLASH_LAYOUT <名稱>` | 分割表，見 [Flash 版面](flash-layout.zh-TW.md) | `realtek-ota` |
| `env RTL8735B_APP_DIR <目錄>` | 開發板的應用程式，見[開發板的應用程式](board-applications.zh-TW.md) | 沒有設 |

## 已移除的設定

這些 `define` 不再有作用：`HAL_RTL8735B_I2C1_*`、`HAL_RTL8735B_SPI1_*`、`HAL_RTL8735B_BMI160_CS_PIN`、`HAL_RTL8735B_RCOUT_CHANNELS`、`HAL_RTL8735B_RCOUT_PIN0`–`PIN3`、`HAL_RTL8735B_GPIO_PIN7`／`PIN25`／`PIN26`、`HAL_RTL8735B_ADC_CHANNELS`、`HAL_RTL8735B_ADC_PIN0`、`HAL_RTL8735B_ADC_LOGICAL0`。疊加檔若還在用，建置不會報錯，但這些設定沒有作用；上面的關鍵字取代它們。
