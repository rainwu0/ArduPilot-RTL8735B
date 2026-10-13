# ArduPilot RTL8735B 移植

Unofficial ArduPilot port to the Realtek RTL8735B SoC. Not flight-tested. Documentation is in Traditional Chinese.

這是把 [ArduPilot](https://github.com/ArduPilot/ardupilot) 搬到 Realtek RTL8735B 上的非官方移植，個人維護，跟 ArduPilot 開發團隊、Realtek 都沒有關係。**還沒飛過，請勿用於飛行。**

細節都在 [HAL 的 README][hal]，這頁只講大概。

## 現在有什麼

- ArduPilot master `9f648ccabc`（2026-09-25）加上 RTL8735B 的 HAL，保留 ArduPilot 的完整歷史。
- 板型：`hub8735ultra`（HUB 8735 ultra，板商 ideasHatch，[板商資料](https://github.com/ideashatch/HUB-8735)），開發都在這塊上做；`amb82-mini` 只建置過。
- 在 HUB 8735 ultra 上能開機，SPI、I²C、UART、有刷馬達的 PWM 都在板上跑過。
- 板型本身不含感測器，要自己接、自己寫進疊加檔。沒接氣壓計時開機停在 Config Error，Wi-Fi 會連上熱點，但不會有 MAVLink；沒接 IMU 時停在 `INS: unable to initialise driver`。

每一項的狀態見[〈功能狀態〉][status]。

## 可以拿來做什麼

- 建置 Copter、燒進板子：[〈取得與建置〉][build]、[〈燒錄〉][flash]。
- 替自己的 RTL8735B 板子寫 hwdef 疊加檔（腳位、匯流排、感測器）：[〈自訂板〉][custom]。
- 把自己的 SDK 端 C 程式和韌體編在一起：[〈板子的應用程式〉][app]。

## 還缺什麼

- 沒飛過。
- 還沒在板上確認：Wi-Fi MAVLink（UDP、TCP）、參數保存、RC 輸入、GPS、OneShot／OneShot125、`--upload`、AMB82-mini。
- 沒有做：DShot、USB MAVLink、SD 卡。
- 已知問題：開機時氣壓計偶爾初始化失敗，原因還沒查到。

## 想參與

- 問題、上板結果都歡迎開 issue，回報在這裡，不要回報到 ArduPilot。上面「還缺什麼」的實測結果特別有用，哪怕只是「我的板子開得了機」。
- 要送 PR 的話先看 [CONTRIBUTING](CONTRIBUTING.md)。

## 最近的變動（2026-10-14）

- **自訂板的寫法改了。** 舊的 `HAL_RTL8735B_I2C1_*`、`HAL_RTL8735B_SPI1_*`、`HAL_RTL8735B_RCOUT_*` 等 `define` 已經拿掉，換成 `RTL8735B_I2CBUS`、`RTL8735B_SPIBUS`、`RTL8735B_SPIDEV`、`RTL8735B_RCOUT`、`RTL8735B_GPIO`、`RTL8735B_ADC` 這些關鍵字。舊的疊加檔建置不會報錯，但設定會悄悄失效，要照[〈自訂板〉][custom]改寫。
- 新增板型 `hub8735ultra`（建議用這個）與 `amb82-mini`。原本的 `rtl8735b-empty` 接法相同，還留著。
- 馬達輸出多了 OneShot 和 OneShot125（都還沒上板）。一般 PWM 改成每次只寫一次暫存器，有些脈寬不會再少 1 µs。
- I²C 傳輸逾時後會重新初始化、必要時清除匯流排，連續 3 次才停用（還沒上板）。
- Wi-Fi 的主控台訊息（加入熱點、斷線等）現在會印到 LOG UART。
- 功能狀態表重新整理過：Wi-Fi MAVLink、參數保存等幾項改成「待複驗」。

更早的變動看 `git log 9f648ccabc..main`。

## 授權

整個儲存庫依 GPL-3.0 散布，見 [COPYING.txt](/COPYING.txt)：ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。建置要用的 Realtek SDK 另有授權條款，不在本儲存庫內，要自己取得；本儲存庫不提供韌體映像。詳見 HAL README 的[〈授權〉][license]。

[hal]: /libraries/AP_HAL_RTL8735B/README.md
[status]: /libraries/AP_HAL_RTL8735B/README.md#功能狀態
[build]: /libraries/AP_HAL_RTL8735B/README.md#取得與建置
[flash]: /libraries/AP_HAL_RTL8735B/README.md#燒錄
[custom]: /libraries/AP_HAL_RTL8735B/README.md#自訂板
[app]: /libraries/AP_HAL_RTL8735B/README.md#板子的應用程式
[license]: /libraries/AP_HAL_RTL8735B/README.md#授權
