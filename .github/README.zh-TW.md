# ArduPilot RTL8735B 移植

[![RTL8735B tests](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml/badge.svg)](https://github.com/rainwu0/ArduPilot-RTL8735B/actions/workflows/rtl8735b_test.yml)

[English](README.md)

ArduPilot 在 Realtek RTL8735B SoC 上的非官方移植。文件有繁體中文與英文。

本儲存庫是 [ArduPilot](https://github.com/ArduPilot/ardupilot) 加上 Realtek RTL8735B 的硬體抽象層（HAL）。一塊 HUB 8735 ultra 加上含 IMU、氣壓計與羅盤的擴充板，用本 HAL 較早的開發版，飛過一台由地面站經 Wi-Fi 遙控的 3.5 吋四軸（T5）。從[從零開始][start]開始。個人維護，跟 ArduPilot 開發團隊、Realtek 都沒有關係。

## 目錄

- [安全](#安全)
- [背景](#背景)
- [本專案的組成](#本專案的組成)
- [安裝](#安裝)
- [使用](#使用)
- [狀態](#狀態)
- [參與](#參與)
- [授權](#授權)

## 安全

- Wi-Fi 上的 MAVLink，不論 UDP 或 TCP，都沒有任何驗證：同一網段的任何裝置都送得到開發板，也可能被當成它的地面站。見 [Wi-Fi][wifi] 的安全上的限制。
- 設定檔裡的 Wi-Fi 帳密會編進韌體映像，拿到映像的人可能讀得出來。沒有設定檔時，開發板連[預設熱點][wifi-default]（`ardupilot`、`ardupilot1`）。
- 安全問題私下回報，方法見 [SECURITY](SECURITY.zh-TW.md)。

## 背景

- ArduPilot master `9f648ccabc`（2026-09-25）加上 RTL8735B 的 HAL，保留 ArduPilot 的完整歷史。HAL 在 `libraries/AP_HAL_RTL8735B/`；對 ArduPilot 共用程式的修改列在 [HAL README][hal]。
- 板型：`hub8735ultra`，用於 ideasHatch 的 HUB 8735 ultra（[板商資料](https://github.com/ideashatch/HUB-8735)），開發與上板測試都在這塊上做；`hub8735ultra-expansion`，HUB 8735 ultra 加擴充板，含 IMU、氣壓計與羅盤；`amb82-mini`；`rtl8735b-empty`，其他板型 include 的設定。每個板型在 [`hwdef/`][boards] 下有 README。
- 除了 `hub8735ultra-expansion`，板型都沒有感測器。感測器、安裝方向與預設參數寫在疊加的 hwdef，見[描述自己的開發板][custom]。

## 本專案的組成

- ArduPilot master `9f648ccabc`，保留完整歷史：[ArduPilot](https://github.com/ArduPilot/ardupilot)
- RTL8735B 的 HAL：[程式](/libraries/AP_HAL_RTL8735B/)、[文件][hal]
- 板型 `hub8735ultra`、`hub8735ultra-expansion`、`amb82-mini`、`rtl8735b-empty`：[`hwdef/`][boards]
- T5 那台四軸的參數：[`Tools/Frame_params/hub8735ultra-expansion-quad-3.5in-2S.param`](/Tools/Frame_params/hub8735ultra-expansion-quad-3.5in-2S.param)
- 經 Wi-Fi 更新：開發板的應用程式 [`app/ota`](/libraries/AP_HAL_RTL8735B/app/ota/)、`Tools/scripts/rtl8735b_ota_sign.py` 與 `Tools/scripts/rtl8735b_ota_send.py`：[文件](/libraries/AP_HAL_RTL8735B/docs/flashing.zh-TW.md#經-wi-fi-更新)
- 建置與測試的腳本：`Tools/ardupilotwaf/rtl8735b.py`、`Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh`、`Tools/scripts/rtl8735b_get_sdk.sh`、`Tools/scripts/rtl8735b_test.sh`：[主機測試][tests]
- GitHub Actions：推到 `main` 與每個 pull request 時跑主機測試與提交檢查（`rtl8735b_test.yml`），交叉建置手動觸發（`rtl8735b_build.yml`）

## 安裝

建置在 Ubuntu 22.04，或 WSL 裡的 Ubuntu 22.04 上進行，需要 Realtek RTL8735B SDK，由腳本取得：

```bash
git clone https://github.com/rainwu0/ArduPilot-RTL8735B.git
cd ArduPilot-RTL8735B
Tools/environment_install/install-rtl8735b-prereqs-ubuntu.sh
source ~/.venv-rtl8735b/bin/activate
Tools/scripts/rtl8735b_get_sdk.sh
export PATH="$PWD/../ambpro2_sdk/tools/asdk-10.3.0/linux/newlib/bin:$PATH"
git submodule update --init --recursive modules/waf modules/mavlink modules/DroneCAN
./waf configure --board hub8735ultra
./waf copter
```

細節，包括在 WSL 裡 clone 到哪裡、Wi-Fi 設定檔，見[建置韌體][build]。本儲存庫不提供韌體映像。

## 使用

- **第一次上手**：照[從零開始][start]一步一步做，從接兩個感測器模組、建置、燒錄，到地面站顯示開發板的姿態。
- T5 那台四軸的硬體：[`hub8735ultra-expansion` 的 README][expansion]。
- 燒錄映像：[燒錄韌體][flash]。
- 替自己的開發板、感測器、腳位與匯流排寫疊加檔：[描述自己的開發板][custom]。
- 經 Wi-Fi（[Wi-Fi][wifi]）或開發板的 USB 序列埠（[HUB 8735 ultra 的 README][hub] 的 SERIAL0）連地面站。
- 接 RC 接收機或 GPS：HUB 8735 ultra 的 README；馬達與電調：[馬達輸出][motors]。
- 把自己的 SDK 端 C 程式編進韌體：[開發板的應用程式][app]。
- 其他文件依種類列在 [HAL README][hal]。

## 狀態

| 項目 | 狀態 |
|---|---|
| 四軸飛行：HUB 8735 ultra 加擴充板（[`hub8735ultra-expansion`][expansion]），地面站經 Wi-Fi 遙控，DShot300 | 能用（T5） |
| 公開版韌體在擴充板上 | 要補：上板驗一次 |
| HUB 8735 ultra 照 [HUB 8735 ultra 的 README][hub] 接上 BMI160、BMP280、GY-271（QMC5883L），地面站經 Wi-Fi（UDP）連線 | 能用（T2） |
| 參數存在 Flash | 能用（T2、T5） |
| 電池電壓與電流 | 要補：校正 |
| 羅盤 | 要補：確認方向並校正 |
| 遙控接收機、GPS | 要補：接上驗一次 |
| 電調遙測、DShot150、DShot600、其他 PWM 輸出 | 要補：接電調驗一次 |
| SD 卡與記錄檔 | 要補：接卡驗一次 |
| USB 序列埠上的 MAVLink、Wi-Fi TCP、預設 Wi-Fi 熱點、`--upload` | 要補：上板驗一次 |
| AMB82-mini | 要補：上板驗一次 |
| 經 Wi-Fi 更新韌體 | 要補：上板驗一次 |

紀錄在 [BOARD_TESTS.md][tests-board]。HAL 各功能的現況見[功能狀態][status]，各開發板的現況見各自 README 的〈規格〉；每一版改了什麼見 [CHANGELOG][changelog]。

不用開發板、不用 SDK 就能跑的主機測試：`Tools/scripts/rtl8735b_test.sh`，說明在 [tests/README.md][tests]。主機測試通過不代表在開發板上能用。

## 參與

- 問題、上板結果都歡迎開 [issue](https://github.com/rainwu0/ArduPilot-RTL8735B/issues)，中文或英文都可以。標「要補」的項目的實測結果特別有用，哪怕只是「我的開發板開得了機」。
- 送 PR：見 [CONTRIBUTING](CONTRIBUTING.zh-TW.md)。

## 授權

GPL-3.0，見 [COPYING.txt](/COPYING.txt)。著作權屬於各檔的作者：ArduPilot 的貢獻者，以及本移植的貢獻者。ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。建置要用的 Realtek SDK 另有授權條款，不在本儲存庫內，要另外取得。詳見 [HAL README][hal] 的〈授權〉。

[hal]: /libraries/AP_HAL_RTL8735B/README.zh-TW.md
[changelog]: /libraries/AP_HAL_RTL8735B/CHANGELOG.zh-TW.md
[start]: /libraries/AP_HAL_RTL8735B/GETTING_STARTED.zh-TW.md
[status]: /libraries/AP_HAL_RTL8735B/docs/feature-status.zh-TW.md
[build]: /libraries/AP_HAL_RTL8735B/docs/building.zh-TW.md
[flash]: /libraries/AP_HAL_RTL8735B/docs/flashing.zh-TW.md
[custom]: /libraries/AP_HAL_RTL8735B/docs/custom-boards.zh-TW.md
[wifi]: /libraries/AP_HAL_RTL8735B/docs/wifi.zh-TW.md
[wifi-default]: /libraries/AP_HAL_RTL8735B/docs/wifi.zh-TW.md#預設熱點
[motors]: /libraries/AP_HAL_RTL8735B/docs/motor-outputs.zh-TW.md
[app]: /libraries/AP_HAL_RTL8735B/docs/board-applications.zh-TW.md
[boards]: /libraries/AP_HAL_RTL8735B/hwdef/
[hub]: /libraries/AP_HAL_RTL8735B/hwdef/hub8735ultra/README.zh-TW.md
[expansion]: /libraries/AP_HAL_RTL8735B/hwdef/hub8735ultra-expansion/README.zh-TW.md
[tests]: /libraries/AP_HAL_RTL8735B/tests/README.md
[tests-board]: /libraries/AP_HAL_RTL8735B/BOARD_TESTS.md
