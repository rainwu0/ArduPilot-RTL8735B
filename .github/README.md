# ArduPilot RTL8735B 移植

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

Unofficial ArduPilot port to the Realtek RTL8735B SoC. Not flight-tested. English: [README.en.md](README.en.md)

這是把 [ArduPilot](https://github.com/ArduPilot/ardupilot) 搬到 Realtek RTL8735B 上的非官方移植，個人維護，跟 ArduPilot 開發團隊、Realtek 都沒有關係。**還沒飛過，請勿用於飛行。**

這頁講整體；各功能的細節在 [HAL 的 README][hal]，每一版改了什麼在 [CHANGELOG][changelog]。

## 現在有什麼

- 目前的版本是 **v0.1.1**（2026-10-19），和 v0.1.0 相容。
- ArduPilot master `9f648ccabc`（2026-09-25）加上 RTL8735B 的 HAL，保留 ArduPilot 的完整歷史。
- 板型：`hub8735ultra`（HUB 8735 ultra，板商 ideasHatch，[板商資料](https://github.com/ideashatch/HUB-8735)），開發都在這塊上做；`amb82-mini` 只建置過。
- **HUB 8735 ultra 照範例接上 BMI160、BMP280、GY-271（QMC5883L）就能用：** 開機、經 Wi-Fi（UDP）連上地面站、參數在重開機後保留、抬起開發板時姿態方向正確。接法和疊加檔見[〈接上感測器〉][sensors]，上板紀錄見 [BOARD_TESTS.md][tests-board] 的 T2。
- 開機時會先清 I²C 匯流排：v0.1.0 開機偶爾出現的氣壓計初始化失敗，在板上反覆熱重開與冷開機時沒有再出現（[BOARD_TESTS.md][tests-board] 的 T3；測的是改成每條匯流排各自一個 task 之前的建置）。
- Copter 主迴圈在板上正常運作時約 400 Hz（120 秒平均 399.9 Hz，[BOARD_TESTS.md][tests-board] 的 T4）。
- 板型本身不含感測器。什麼都沒接時開機停在氣壓計的 Config Error，Wi-Fi 會連上熱點，但不會有 MAVLink。
- 不用開發板、不用 SDK 就能跑的主機測試：`Tools/scripts/rtl8735b_test.sh`，說明在 [tests/README.md][tests]。主機測試通過不代表在開發板上能用。

每一項的狀態見[〈功能狀態〉][status]。

## 可以拿來做什麼

- **第一次上手**：照[〈從零開始〉][start]一步一步做，從接線、建置、燒錄到地面站連上。
- 建置 Copter、燒進開發板的細節：[〈取得與建置〉][build]、[〈燒錄〉][flash]。
- 替自己的 RTL8735B 開發板寫 hwdef 疊加檔（腳位、匯流排、感測器）：[〈自訂板〉][custom]。最多可以設四條 I²C 匯流排，例如外接的感測器另走一條（還沒上板）。
- 接 RC 接收機（CRSF、ELRS 等）或 GPS：接法和預設序列埠在[〈板型〉][boards]（還沒接過實物）。
- 把自己的 SDK 端 C 程式和韌體編在一起：[〈開發板的應用程式〉][app]。

## 還缺什麼

- 沒飛過。
- 還沒在板上確認：
  - 羅盤的方向與校正：目前只確認偵測得到；
  - roll 的正負：由 pitch 的結果推得，沒有直接測；
  - 斷電後參數是否保留：只測了重開機；
  - 多條 I²C 匯流排同時用；
  - UART、PWM 輸出、Wi-Fi TCP、RC 輸入、GPS、`--upload`、AMB82-mini。
- 範例疊加檔的 IMU 方向，是在範例板的擺法下實測的。換了擺法要重測，方法寫在〈接上感測器〉。
- 沒有做：電壓換算（所以不能用類比電池監測）、DShot、USB MAVLink、SD 卡。
- 參數換扇區後的那次開機，如果抹除期間斷電或重置，參數會全部回到預設。請用地面站存一份參數檔，見[〈Flash 版面〉][layout]。

## 想參與

- 問題、上板結果都歡迎開 issue，回報在這裡，不要回報到 ArduPilot。上面「還缺什麼」的實測結果特別有用，哪怕只是「我的開發板開得了機」。
- 要送 PR 的話先看 [CONTRIBUTING](CONTRIBUTING.md)，送之前跑一次 `Tools/scripts/rtl8735b_test.sh`。PR 也會跑同一套測試；外部貢獻者送的 PR，要等維護者核准才會開始跑。

## 最近的變動

### v0.1.1（2026-10-19）：開機更穩，可以接第二條 I²C

和 v0.1.0 相容：原本的疊加檔、指令與參數照舊能用。兩個修正都要重新建置、燒錄才會生效。標籤是 `v0.1.1`（`git checkout v0.1.1`），只標提交，不附韌體。

- 修正：開機時先清 I²C 匯流排再初始化控制器，重新初始化時遮罩中斷。v0.1.0 開機偶爾出現的 `Config Error: Baro`，在板上反覆熱重開與冷開機時沒有再出現（BOARD_TESTS 的 T3；測的是改成每條匯流排各自一個 task 之前的建置，v0.1.1 這一版的程式還沒上板）。
- 修正：IMU 停止送樣本時（例如線鬆脫），主執行緒不再一直忙等，Wi-Fi、UART 等優先權較低的工作還能執行。地面站的 MAVLink 由主迴圈產生，主迴圈卡住時一樣收不到。只經主機測試驗證，還沒上板。
- 新增：一個 hwdef 最多四條 I²C 匯流排，各自計逾時與停用，見[〈自訂板〉][custom]。只經建置與主機測試，還沒上板。
- 新增說明：RC 接收機（CRSF、ELRS 等，不支援 SBUS）與 GPS 的接法和預設序列埠，見[〈板型〉][boards]。附主機測試，還沒接過實物。
- 新增說明：參數換扇區後那次開機的抹除窗口，見上面〈還缺什麼〉的最後一條。

上一版 v0.1.0（2026-10-18）是第一個版本：照範例接上感測器，就能在 HUB 8735 ultra 上開機、連上地面站。每一版的完整變更見 [CHANGELOG][changelog]。

## 授權

整個儲存庫依 GPL-3.0 散布，見 [COPYING.txt](/COPYING.txt)：ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。建置要用的 Realtek SDK 另有授權條款，不在本儲存庫內，要自己取得；本儲存庫不提供韌體映像。詳見 HAL README 的[〈授權〉][license]。

[hal]: /libraries/AP_HAL_RTL8735B/README.md
[changelog]: /libraries/AP_HAL_RTL8735B/CHANGELOG.md
[status]: /libraries/AP_HAL_RTL8735B/README.md#功能狀態
[build]: /libraries/AP_HAL_RTL8735B/README.md#取得與建置
[boards]: /libraries/AP_HAL_RTL8735B/README.md#板型
[flash]: /libraries/AP_HAL_RTL8735B/README.md#燒錄
[custom]: /libraries/AP_HAL_RTL8735B/README.md#自訂板
[layout]: /libraries/AP_HAL_RTL8735B/README.md#flash-版面
[app]: /libraries/AP_HAL_RTL8735B/README.md#開發板的應用程式
[license]: /libraries/AP_HAL_RTL8735B/README.md#授權
[tests]: /libraries/AP_HAL_RTL8735B/tests/README.md
[tests-board]: /libraries/AP_HAL_RTL8735B/BOARD_TESTS.md
[start]: /libraries/AP_HAL_RTL8735B/GETTING_STARTED.md
[sensors]: /libraries/AP_HAL_RTL8735B/README.md#接上感測器
