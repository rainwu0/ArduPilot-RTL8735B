# ArduPilot RTL8735B 移植

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

Unofficial ArduPilot port to the Realtek RTL8735B SoC. Not flight-tested. Documentation is in Traditional Chinese.

這是把 [ArduPilot](https://github.com/ArduPilot/ardupilot) 搬到 Realtek RTL8735B 上的非官方移植，個人維護，跟 ArduPilot 開發團隊、Realtek 都沒有關係。**還沒飛過，請勿用於飛行。**

細節都在 [HAL 的 README][hal]，這頁只講大概。

## 現在有什麼

- ArduPilot master `9f648ccabc`（2026-09-25）加上 RTL8735B 的 HAL，保留 ArduPilot 的完整歷史。
- 板型：`hub8735ultra`（HUB 8735 ultra，板商 ideasHatch，[板商資料](https://github.com/ideashatch/HUB-8735)），開發都在這塊上做；`amb82-mini` 只建置過。
- **HUB 8735 ultra 照範例接上 BMI160、BMP280、GY-271（QMC5883L）就能用：** 開機、經 Wi-Fi（UDP）連上地面站、參數在重開機後保留、抬起開發板時姿態方向正確。接法和疊加檔見[〈接上感測器〉][sensors]，上板紀錄見 [BOARD_TESTS.md][tests-board] 的 T2。
- 板型本身不含感測器。什麼都沒接時開機停在氣壓計的 Config Error，Wi-Fi 會連上熱點，但不會有 MAVLink。
- 不用開發板、不用 SDK 就能跑的主機測試：`Tools/scripts/rtl8735b_test.sh`，說明在 [tests/README.md][tests]。主機測試通過不代表在開發板上能用。

每一項的狀態見[〈功能狀態〉][status]。

## 可以拿來做什麼

- 建置 Copter、燒進開發板：[〈取得與建置〉][build]、[〈燒錄〉][flash]。第一次上手，照[〈接上感測器〉][sensors]接三顆模組最快。
- 替自己的 RTL8735B 開發板寫 hwdef 疊加檔（腳位、匯流排、感測器）：[〈自訂板〉][custom]。
- 把自己的 SDK 端 C 程式和韌體編在一起：[〈板子的應用程式〉][app]。

## 還缺什麼

- 沒飛過。
- 還沒在板上確認：
  - 羅盤的方向與校正：目前只確認偵測得到；
  - roll 的正負：由 pitch 的結果推得，沒有直接測；
  - 斷電後參數是否保留：只測了重開機；
  - 主迴圈的實際頻率；
  - UART、PWM 輸出、Wi-Fi TCP、RC 輸入、GPS、`--upload`、AMB82-mini。
- 範例疊加檔的 IMU 方向，是在範例板的擺法下實測的。換了擺法要重測，方法寫在〈接上感測器〉。
- 沒有做：電壓換算（所以不能用類比電池監測）、DShot、USB MAVLink、SD 卡。
- 已知問題：開機時氣壓計偶爾初始化失敗，原因還沒查到。T2 碰到一次，按 reset 重開後就正常了。

## 想參與

- 問題、上板結果都歡迎開 issue，回報在這裡，不要回報到 ArduPilot。上面「還缺什麼」的實測結果特別有用，哪怕只是「我的開發板開得了機」。
- 要送 PR 的話先看 [CONTRIBUTING](CONTRIBUTING.md)，送之前跑一次 `Tools/scripts/rtl8735b_test.sh`。PR 也會跑同一套測試；外部貢獻者送的 PR，要等維護者核准才會開始跑。

## 最近的變動

### v0.1.0：HUB 8735 ultra 接上感測器能用

第一個里程碑：照範例接線，就能在 HUB 8735 ultra 上開機、連上地面站。這個版本標了標籤 `v0.1.0`（`git checkout v0.1.0`）。標籤只標提交，不附韌體。

- 新增範例疊加檔 `hwdef/hub8735ultra/sensors-example.dat`，以及 HAL README 的〈接上感測器〉：BMI160 走 SPI，BMP280、GY-271 走 I²C，附接線表。
- 新增 [BOARD_TESTS.md][tests-board]，記錄每一次上板驗證：版本、映像、接線、步驟、結果和沒測到的部分。功能狀態表的「板上驗證」都附紀錄編號。
- SPI、I²C、排程、Wi-Fi MAVLink（UDP）、參數保存，從「待複驗」改成「板上驗證（T2）」。參數保存只驗了重開機。
- 限制照實寫在上面〈還缺什麼〉：還沒飛過，羅盤方向與校正還沒驗，roll 方向是推得的。

### 2026-10-14：公開測試

- 新增 `Tools/scripts/rtl8735b_test.sh`：hwdef、Flash 版面、`--upload` 的工具測試，以及 HAL（序列埠、SPI／I²C、IO、RC 輸入、參數儲存、排程、Wi-Fi）的主機測試。需要 `build-essential` 和 `python3`，不需要開發板和 SDK。加 `--build` 會另外交叉建置一次。
- 推到 `main` 和開 PR 時，GitHub Actions 會跑同一套測試和提交慣例檢查；交叉建置要手動觸發。原本從 ArduPilot 帶過來、在這裡用不到的 CI 設定已經拿掉。

更早的變動看 `git log 9f648ccabc..main`。2026-10-14 以前的疊加檔要注意：舊的 `HAL_RTL8735B_*` 腳位設定已改成關鍵字（見[〈自訂板〉][custom]），同一支腳用兩次會在 configure 時失敗，開發板的應用程式少了 `rtl8735b_app_init()` 會連結失敗。

## 授權

整個儲存庫依 GPL-3.0 散布，見 [COPYING.txt](/COPYING.txt)：ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。建置要用的 Realtek SDK 另有授權條款，不在本儲存庫內，要自己取得；本儲存庫不提供韌體映像。詳見 HAL README 的[〈授權〉][license]。

[hal]: /libraries/AP_HAL_RTL8735B/README.md
[status]: /libraries/AP_HAL_RTL8735B/README.md#功能狀態
[build]: /libraries/AP_HAL_RTL8735B/README.md#取得與建置
[flash]: /libraries/AP_HAL_RTL8735B/README.md#燒錄
[custom]: /libraries/AP_HAL_RTL8735B/README.md#自訂板
[app]: /libraries/AP_HAL_RTL8735B/README.md#板子的應用程式
[license]: /libraries/AP_HAL_RTL8735B/README.md#授權
[tests]: /libraries/AP_HAL_RTL8735B/tests/README.md
[tests-board]: /libraries/AP_HAL_RTL8735B/BOARD_TESTS.md
[sensors]: /libraries/AP_HAL_RTL8735B/README.md#接上感測器
