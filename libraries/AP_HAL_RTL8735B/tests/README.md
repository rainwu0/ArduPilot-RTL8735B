# RTL8735B HAL 的測試

在儲存庫根目錄執行：

```bash
Tools/scripts/rtl8735b_test.sh
```

不需要開發板，也不需要 Realtek SDK。需要 `gcc`／`g++`（C11、C++17）、`python3`（3.10 以上）、`bash` 與 `timeout`；Ubuntu 22.04 裝 `build-essential` 與 `python3` 就夠。跑完印每一項的結果，任何一項失敗就以非 0 結束。

這裡的測試不是 waf 的 gtest，`./waf tests` 不會跑到它們。

## 內容

| 項目 | 測什麼 |
|---|---|
| `test_rtl8735b_hwdef_pins.py` | hwdef 的開發板描述關鍵字、板型、同腳與範圍檢查 |
| `test_rtl8735b_flash_layout.py` | Flash 版面、分割表、映像大小檢查 |
| `test_sdk_extract.py` | 建置時從 SDK 取得原始碼清單的工具 |
| `test_rtl8735b_upload.py` | `--upload` 燒錄的參數與流程 |
| `hal/` | 序列埠、SPI／I²C、IO（GPIO、類比輸入、PWM、看門狗）、RC 輸入、參數儲存、排程 |
| `hal_wifi/` | Wi-Fi 帳密產生器、SDK 的 C 包裝、WiFiDriver 的 UDP 與 TCP |

- `hal/`、`hal_wifi/` 把正式的 HAL 原始碼和替身（`fakes/` 等目錄裡的 SDK、FreeRTOS、AP_HAL）一起在主機上編譯執行。替身不是硬體模型：主機測試通過，不代表在開發板上能用。板上的驗證結果見 HAL README〈功能狀態〉。
- `hal/run.sh`、`hal_wifi/run.sh` 也可以單獨執行；`hal/run.sh` 給第二個參數（`uart`、`bus`、`io`、`rc_storage`、`scheduler`）只跑其中一組。

## 要 SDK 才跑的項目

設了環境變數 `AMEBAPRO2_SDK`（SDK 的目錄）才會跑，沒設就略過，不算失敗：

- `test_sdk_extract.py`：用真的 SDK 原始碼清單驗證；
- `test_rtl8735b_upload.py`：從 SDK 取出燒錄工具；
- `test_rtl8735b_flash_layout.py`：以 SDK 的分割表 JSON 為底產生分割表。

`Tools/scripts/rtl8735b_test.sh --build` 另外交叉建置一次 `hub8735ultra` 的 copter，需要的環境照 HAL README〈取得與建置〉。

## 加測試

- 修正錯誤時，先寫一個在修正前會失敗的測試，放在對應的檔案或 `run.sh` 的那一組裡。
- 測試檔與替身照 HAL 原始碼的授權（GPL-3.0-or-later）。
