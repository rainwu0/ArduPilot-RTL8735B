# ArduPilot RTL8735B 移植

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

Unofficial ArduPilot port to the Realtek RTL8735B SoC. Not flight-tested. English: [README.en.md](README.en.md)

這是把 [ArduPilot](https://github.com/ArduPilot/ardupilot) 搬到 Realtek RTL8735B 上的非官方移植，個人維護，跟 ArduPilot 開發團隊、Realtek 都沒有關係。**還沒飛過，請勿用於飛行。**

這頁講整體；各功能的細節在 [HAL 的 README][hal]，每一版改了什麼在 [CHANGELOG][changelog]。

## 現在有什麼

- 目前的版本是 **v0.2.0**（2026-10-22）。和 v0.1.1 有兩處不相容，見〈最近的變動〉。
- ArduPilot master `9f648ccabc`（2026-09-25）加上 RTL8735B 的 HAL，保留 ArduPilot 的完整歷史。
- 板型：`hub8735ultra`（HUB 8735 ultra，板商 ideasHatch，[板商資料](https://github.com/ideashatch/HUB-8735)），開發都在這塊上做；`amb82-mini` 只建置過。
- **HUB 8735 ultra 照範例接上 BMI160、BMP280、GY-271（QMC5883L）就能用：** 開機、經 Wi-Fi（UDP）連上地面站、參數在重開機後保留、抬起開發板時姿態方向正確。接法、疊加檔與參數在[範例庫][examples]的第一個範例 `bmi160-bmp280-qmc5883l`，上板紀錄見 [BOARD_TESTS.md][tests-board] 的 T2。
- 開機時會先清 I²C 匯流排：v0.1.0 開機偶爾出現的氣壓計初始化失敗，在板上反覆熱重開與冷開機時沒有再出現（[BOARD_TESTS.md][tests-board] 的 T3；測的是改成每條匯流排各自一個 task 之前的建置）。
- Copter 主迴圈在板上正常運作時約 400 Hz（120 秒平均 399.9 Hz，[BOARD_TESTS.md][tests-board] 的 T4）。
- v0.2.0 加了主控台 MAVLink（經開發板的 USB 序列埠連地面站）、類比電壓、microSD 卡、DShot 與電調遙測、只收的序列埠：都只經建置與主機測試，還沒上板。
- 板型本身不含感測器。什麼都沒接時開機停在氣壓計的 Config Error，Wi-Fi 會連上熱點，但不會有 MAVLink。
- 不用開發板、不用 SDK 就能跑的主機測試：`Tools/scripts/rtl8735b_test.sh`，說明在 [tests/README.md][tests]。主機測試通過不代表在開發板上能用。

每一項的狀態見[〈功能狀態〉][status]。

## 可以拿來做什麼

- **第一次上手**：照[〈從零開始〉][start]一步一步做，從接線、建置、燒錄到地面站連上。
- 建置 Copter、燒進開發板的細節：[〈取得與建置〉][build]、[〈燒錄〉][flash]。
- 替自己的 RTL8735B 開發板寫 hwdef 疊加檔（腳位、匯流排、感測器）：[〈自訂板〉][custom]。最多可以設四條 I²C 匯流排，例如外接的感測器另走一條（還沒上板）。
- 接 RC 接收機（CRSF、ELRS 等）或 GPS：接法和預設序列埠在[〈板型〉][boards]（還沒接過實物）。
- 把自己的 SDK 端 C 程式和韌體編在一起：[〈開發板的應用程式〉][app]。
- 照現成的範例接線、建置與載入參數：[範例庫][examples]。目前有兩個範例：三片感測器模組，以及一塊手焊的擴充板（另有電池電壓、馬達、電調、GPS、遙控接收機與第二條 I²C 的排針）。
- 沒有 Wi-Fi 時，經開發板的 USB 序列埠（燒錄用的那個）連地面站：見[〈板型〉][boards]的主控台 MAVLink（還沒上板）。
- 量電池電壓、把記錄寫進 microSD 卡、用 DShot 電調與電調遙測：[〈自訂板〉][custom]、[〈SD 卡〉][sd]、[〈馬達輸出〉][motors]（都還沒上板）。

## 還缺什麼

- 沒飛過。
- 還沒在板上確認：
  - 羅盤的方向與校正：目前只確認偵測得到；
  - roll 的正負：由 pitch 的結果推得，沒有直接測；
  - 斷電後參數是否保留：只測了重開機；
  - 多條 I²C 匯流排同時用；
  - UART、PWM 輸出、Wi-Fi TCP、RC 輸入、GPS、`--upload`、AMB82-mini；
  - v0.2.0 新加的全部：主控台 MAVLink、類比電壓、SD 卡、DShot 與電調遙測、只收的序列埠、Wi-Fi 湊包與重試、主迴圈停住時停送輸出、抹除參數區時擋解鎖、開發板應用程式的新介面。
- 範例庫第一個範例的 IMU 方向，是在範例板的擺法下實測的。換了擺法要重測，方法寫在那個範例的 README。
- 沒有做：雙向 DShot、DShot1200、DShot 的 3D；晶片自己的 USB（開發板另一個 USB-C）不做 MAVLink。
- 參數換扇區後的那次開機，如果抹除期間斷電或重置，參數會全部回到預設。請用地面站存一份參數檔，見[〈Flash 版面〉][layout]。

## 想參與

- 問題、上板結果都歡迎開 issue，回報在這裡，不要回報到 ArduPilot。上面「還缺什麼」的實測結果特別有用，哪怕只是「我的開發板開得了機」。
- 要送 PR 的話先看 [CONTRIBUTING](CONTRIBUTING.md)，送之前跑一次 `Tools/scripts/rtl8735b_test.sh`。PR 也會跑同一套測試；外部貢獻者送的 PR，要等維護者核准才會開始跑。

## 最近的變動

### v0.2.0（2026-10-22）：USB 序列埠的 MAVLink、電壓、SD 卡、DShot（都還沒上板）

和 v0.1.1 有兩處不相容，原本的其他疊加檔、指令與參數照舊能用。新功能要重新建置、燒錄才有，都只經建置與主機測試，還沒上板。標籤是 `v0.2.0`（`git checkout v0.2.0`），只標提交，不附韌體。

- **不相容：範例疊加檔 `hwdef/hub8735ultra/sensors-example.dat` 移到[範例庫][examples]。** 建置時改用範例庫的 `examples/bmi160-bmp280-qmc5883l/overlay.dat`，感測器設定相同。
- **不相容：SERIAL0（LOG UART，PF3、PF4）預設打開，跑 MAVLink2。** 疊加檔把這兩支腳拿去當別的用途時，要先關掉 SERIAL0，寫法見 [CHANGELOG][changelog]。
- 新增：主控台 MAVLink。地面站接開發板的 USB 序列埠、115200 baud 就能連線，不需要 Wi-Fi；燒錄前先讓地面站斷開這個埠。見[〈板型〉][boards]。
- 新增：類比輸入換算成電壓，可以用類比電池監測；hwdef 可以列多支類比腳。見[〈自訂板〉][custom]。
- 新增：DShot150、DShot300、DShot600 與電調遙測（TLM 線），見[〈馬達輸出〉][motors]。
- 新增：microSD 卡（FatFs），地面站的 MAVLink FTP 看得到卡上的檔案，記錄可以寫進卡；見[〈SD 卡〉][sd]。
- 新增：只收的序列埠；開發板應用程式的新呼叫點與查詢，見[〈開發板的應用程式〉][app]。
- 變更（安全）：解鎖中主迴圈超過 100 ms 沒跑完一圈時，PWM 停送、DShot 改送油門 0；執行中抹除參數區時，一般解鎖被擋。
- 變更：Wi-Fi UDP 把幾則 MAVLink 湊成一個資料包再送（每則最多晚約 10 ms），驅動緩衝不足時重試；執行中的警告也送到地面站。

上一版 v0.1.1（2026-10-19）：開機時先清 I²C 匯流排，板上反覆開機沒有再出現氣壓計初始化失敗；一個 hwdef 可以設四條 I²C 匯流排。每一版的完整變更見 [CHANGELOG][changelog]。

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
[sd]: /libraries/AP_HAL_RTL8735B/README.md#sd-卡
[motors]: /libraries/AP_HAL_RTL8735B/README.md#馬達輸出
[examples]: https://github.com/rainwu0/ArduPilot-Hub8735Ultra
