# 描述自己的開發板

[English](custom-boards.md)

怎麼以疊加檔把板型改成自己的開發板、感測器與接法。疊加檔是另一個 hwdef 檔，在 configure 時加上。這裡用到的每個設定都列在 [hwdef 設定](hwdef.zh-TW.md)。

## 以疊加檔建置

```bash
./waf configure --board hub8735ultra --extra-hwdef /path/to/myboard.dat
./waf copter
```

- 從最接近的板型開始（[`hub8735ultra`](../hwdef/hub8735ultra/README.zh-TW.md)、[`hub8735ultra-expansion`](../hwdef/hub8735ultra-expansion/README.zh-TW.md)、[`amb82-mini`](../hwdef/amb82-mini/README.zh-TW.md) 或 [`rtl8735b-empty`](../hwdef/rtl8735b-empty/README.zh-TW.md)），疊加檔只寫不同的部分。
- 疊加檔不是板型，所以 `./waf list_boards` 不會多出新的板子。

## 加上感測器

板型都沒有感測器。沒有氣壓計時，開機停在 `Config Error: Baro: unable to initialise driver`，Wi-Fi 也不會有 MAVLink（[原因](design.zh-TW.md#wi-fi-mavlink-要等氣壓計)）。

感測器、安裝方向與預設參數寫在疊加檔，寫法和 ArduPilot 其他平台的 hwdef 相同，例如 ArduPilot `9f648ccabc` 的 `libraries/AP_HAL_ESP32/hwdef/esp32diy/hwdef.dat`。HUB 8735 ultra 上 SPI 接 BMI160、I²C 接 BMP280 與 QMC5883L、實際能用的疊加檔，是範例庫的 [`examples/bmi160-bmp280-qmc5883l/overlay.dat`](https://github.com/rainwu0/ArduPilot-Hub8735Ultra/blob/v0.1.0/examples/bmi160-bmp280-qmc5883l/overlay.dat)（rainwu0/ArduPilot-Hub8735Ultra `v0.1.0`）。用其他感測器時，複製一份，改 `IMU`、`BARO`、`COMPASS` 三行。

IMU 的 `ROTATION_*` 要實測決定，不能只看模組上的絲印：開發板放平時 roll、pitch 接近 0，抬起開發板前端時 pitch 為正。整塊開發板在機架上轉了方向時，用參數 `AHRS_ORIENTATION` 調整，不必改疊加檔。

## 改腳位、匯流排與輸出

每一行關鍵字加一筆。要換掉板型的一整組，先以 `undef <關鍵字>` 清空，再把每一筆重寫。板型 `amb82-mini` 對 GPIO 就是這樣做：

```text
undef RTL8735B_GPIO
RTL8735B_GPIO 17 PIN_D17 in
RTL8735B_GPIO 24 PIN_E6 out
RTL8735B_GPIO 23 PIN_F9 out
```

序列埠的設定以 `undef` 後重新 `define` 取代。同一支腳被兩個設定用到時，configure 失敗並指出兩個來源；先把原本用到這支腳的設定拿掉。

## 為外接感測器加一條 I²C 匯流排

感測器留在匯流排 1、外接的另走一條時，加一條用另一個 SDK 編號的匯流排。HUB 8735 ultra 的 D3、D4 可以當 SDK 的 I2C2：

```text
RTL8735B_I2CBUS 2 2 PIN_E4 PIN_E3 400000 external
```

- 接頭拉到板外的匯流排標成 `external`：ArduPilot 只在這種匯流排上探測外接的感測器。
- 同一條匯流排上的兩個裝置要不同位址（範例接法的匯流排 1 有 0x76 的 BMP280、0x0D 的 QMC5883L）。衝突時改裝置的位址腳，或把其中一個移到另一條匯流排。

## 使用外接氣壓計或空速計

這兩種不改疊加檔就能用。I²C 氣壓計設參數 `BARO_PROBE_EXT`（要找的型號，各位元見 ArduPilot 的參數說明）後重開機，在 `external` 匯流排上找。空速計設 `ARSPD_TYPE`（預設 0，不啟用；Plane 也是）。兩者的現況見[功能狀態](feature-status.zh-TW.md)。

## 加上外接羅盤

外接羅盤要在疊加檔寫 `COMPASS` 列，連同它的方向。

沒有 `COMPASS` 列的疊加檔，可以改加 `define AP_COMPASS_PROBING_ENABLED 1`：開機時在 `external` 匯流排找所有編進來的型號，方向用驅動的預設值（例如 QMC5883L 是 `ROTATION_ROLL_180`），再以參數 `COMPASS_ORIENT` 調整。疊加檔有 `COMPASS` 列時，自動搜尋會先以預設方向登錄羅盤（[原因](design.zh-TW.md#外接羅盤預設不自動搜尋)）。

## 序列埠只收，空出 TX 腳

把一個埠的 TX 腳設成 `PIN_NC`，這個埠就只收（例如電調遙測，`SERIALn_PROTOCOL` 16）；寫入的資料丟掉，TX 腳空出來給別的用途。SERIAL0（LOG UART）不適用：這樣設時它不會開啟。例如在 HUB 8735 ultra 上以 D10（PA3）收電調遙測、D9（PA2）量電調電流：

```text
undef HAL_RTL8735B_SERIAL3_TX_PIN
define HAL_RTL8735B_SERIAL3_TX_PIN PIN_NC
RTL8735B_ADC 9 PIN_A2
```

這時 GPS 改接 SERIAL1。

## 關掉開發板沒有的功能

| 要關掉 | 疊加檔的寫法 |
|---|---|
| SERIAL0（LOG UART 上的 MAVLink） | `undef HAL_RTL8735B_SERIAL0_ENABLED`，再 `define HAL_RTL8735B_SERIAL0_ENABLED 0` |
| microSD 卡 | `undef HAL_RTL8735B_SDCARD_ENABLED` |
| 經序列埠的電調遙測 | `undef HAL_SUPPORT_RCOUT_SERIAL` |

## Wi-Fi MAVLink 改用 TCP

```text
define HAL_RTL8735B_WIFI_TCP_PORT 5760
```

地面站改連開發板的 5760 埠；見 [Wi-Fi](wifi.zh-TW.md)。

## 選擇 Flash 版面

```text
FLASH_LAYOUT sdk-d1b6426
```

改了 `FLASH_LAYOUT` 之後要重新 configure。各版面見 [Flash 版面](flash-layout.zh-TW.md)。

## 編進開發板的應用程式

```text
env RTL8735B_APP_DIR <目錄>
```

介面，以及在沒有感測器的開發板上怎麼執行應用程式，見[開發板的應用程式](board-applications.zh-TW.md)。
