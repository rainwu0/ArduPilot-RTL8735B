# ArduPilot RTL8735B 移植

[English](README.md)

讓 ArduPilot 在 Realtek RTL8735B 上執行的硬體抽象層（HAL）。HUB 8735 ultra 用這個 HAL 在 3.5 吋四軸上飛過，見[功能狀態](docs/feature-status.zh-TW.md)。

本儲存庫是個人維護的非官方移植，與 ArduPilot 開發團隊無關。本平台的問題在[本儲存庫的 issue](https://github.com/rainwu0/ArduPilot-RTL8735B/issues) 回報。

## 板型

| 板型 | 開發板 | 上板測試 |
|---|---|---|
| [`hub8735ultra`](hwdef/hub8735ultra/README.zh-TW.md) | HUB 8735 ultra（ideasHatch） | 開發與到目前為止的上板測試都用這塊板子 |
| [`hub8735ultra-expansion`](hwdef/hub8735ultra-expansion/README.zh-TW.md) | HUB 8735 ultra 加擴充板（IMU、氣壓計、羅盤） | T5，這套硬體的飛行 |
| [`amb82-mini`](hwdef/amb82-mini/README.zh-TW.md) | AMB82-mini | 見它的[現況](hwdef/amb82-mini/README.zh-TW.md#現況) |
| [`rtl8735b-empty`](hwdef/rtl8735b-empty/README.zh-TW.md) | 無：其他板型 include 的設定 | 沒有自己的板上紀錄 |

## 文件

教學：

- [從零開始](GETTING_STARTED.zh-TW.md)：從一塊 HUB 8735 ultra 與兩個感測器模組，到地面站顯示開發板的姿態。

操作指南：

- [建置韌體](docs/building.zh-TW.md)
- [燒錄韌體](docs/flashing.zh-TW.md)
- [描述自己的開發板](docs/custom-boards.zh-TW.md)：感測器、腳位、匯流排、序列埠與其他疊加檔設定

參考：

- [功能狀態](docs/feature-status.zh-TW.md)：各功能的現況（能用、要補、沒有）
- [hwdef 設定](docs/hwdef.zh-TW.md)
- [馬達輸出](docs/motor-outputs.zh-TW.md)：PWM、OneShot、有刷、DShot 與電調遙測
- [Wi-Fi](docs/wifi.zh-TW.md)
- [SD 卡](docs/sd-card.zh-TW.md)
- [Flash 版面](docs/flash-layout.zh-TW.md)：分割與參數儲存
- [開發板的應用程式](docs/board-applications.zh-TW.md)：編進韌體的 SDK 端 C 程式的介面
- [CHANGELOG](CHANGELOG.zh-TW.md)
- [BOARD_TESTS.md](BOARD_TESTS.md)：上板測試的紀錄
- [tests/README.md](tests/README.md)：主機測試

說明：

- [設計說明](docs/design.zh-TW.md)：本移植為什麼這樣運作，在看不出原因的地方

## 對 ArduPilot 共用程式的修改

- `ArduCopter/takeoff_check.cpp`：沒有 ESC 遙測的建置（`HAL_WITH_ESC_TELEM` 為 0，例如疊加檔 `undef HAL_SUPPORT_RCOUT_SERIAL` 的自訂板），原本的起飛前檢查整段不編譯，解鎖後馬達會停在地面怠速。改為沒有 ESC 遙測時只略過馬達轉速檢查，CPU 負載檢查照常（本 HAL 尚未提供系統負載，所以目前不會擋）；有 ESC 遙測的建置（板型的預設）不受影響，照上游原本的檢查。本庫直接採用上游 [PR 34070](https://github.com/ArduPilot/ardupilot/pull/34070)（作者 yuiseki）的提交；到 2026-10-07 這個 PR 尚未被上游合併。
- `libraries/AP_HAL/AP_HAL_Boards.h`：`HAL_BOARD_RTL8735B` 為 100，避開上游依序分配的平台編號。
- `libraries/AP_HAL/board/rtl8735b.h`（新檔）：本平台的預設值，例如板名、記憶體等級、參數區大小、SD 卡與記錄目錄的開關、程式大小上限與號誌型別，和其他平台的 `libraries/AP_HAL/board/*.h` 放在同一處；只在本平台引入，不改變其他平台的行為。
- `Tools/ardupilotwaf/boards.py`（waf 的板型登記）與 `libraries/AP_Filesystem/AP_Filesystem.h`（本平台的 newlib 沒有 `dirent.h`，在此補上定義）：加入本平台，不改變其他平台的行為。
- `libraries/AP_Filesystem/AP_Filesystem.cpp`、`AP_Filesystem_config.h`：加入本平台的 SD 卡後端 `AP_Filesystem_RTL8735B`（新檔），不改變其他平台的行為。

## 授權

本儲存庫整體依 GPL-3.0 散布：ArduPilot 與本移植的檔案多為 GPL-3.0-or-later，個別上游檔案另有相容的授權，以各檔檔頭為準。Realtek SDK 另有授權條款（SDK 根目錄的 `Realtek_Disclaimer-2019.pdf`），不在本儲存庫內，要另外取得。建置出的韌體含 Realtek 的預編譯函式庫；本儲存庫不提供韌體映像，Releases 與 CI 產物也不提供。自行建置的韌體要散布前，須同時符合 GPL-3.0 與 Realtek 的條款。

本移植有幾處改寫自 ArduPilot 的 ESP32 移植（ArduPilot `9f648ccabc` 的 `libraries/AP_HAL_ESP32` 與 `Tools/ardupilotwaf/esp32.py`）：`Semaphores.cpp`、`Semaphores.h`、`system.cpp`、`hwdef/scripts/rtl8735b_hwdef.py`，以及 `Tools/ardupilotwaf/rtl8735b.py` 中 hwdef 產生與 cmake 設定的部分，各檔檔頭有註明。Scheduler 分成哪些 FreeRTOS task 也依照 ESP32 移植的設計。兩者同為 GPL-3.0-or-later。

SD 卡的檔案系統後端 `libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp` 改寫自 ArduPilot `9f648ccabc` 的 `AP_Filesystem_FATFS.cpp`，檔頭有註明。`fatfs/` 的 FatFs 依 ChaN 的 FatFs 授權（BSD 型，見 `fatfs/LICENSE.txt` 與各檔檔頭），可以和 GPL-3.0 一起散布。
