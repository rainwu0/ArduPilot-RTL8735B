# 安全政策

[English](SECURITY.md)

本政策涵蓋這個移植：本儲存庫裡的 RTL8735B HAL、它的板型與建置腳本。

## 支援的版本

開發在 `main` 上進行，修正放進 `main` 與下一版。每一版的改動見 [CHANGELOG](/libraries/AP_HAL_RTL8735B/CHANGELOG.zh-TW.md)。

## 回報弱點

經 GitHub 私下回報：本儲存庫 Security 分頁的 [Report a vulnerability](https://github.com/rainwu0/ArduPilot-RTL8735B/security/advisories/new)。回報與討論在公告發布之前都不公開。

盡量附上：

- 本儲存庫的提交（`git rev-parse --short HEAD`）與板型；
- 攻擊者需要什麼條件（例如在同一個 Wi-Fi 網段，或拿到映像檔）；
- 重現的步驟，以及會發生什麼。

用 Wi-Fi 設定檔建置的映像含有帳密。附上的映像不設 `RTL8735B_WIFI_CONFIG` 建置，疊加檔與設定檔拿掉帳密再附。

同時影響 ArduPilot 其他平台的問題，也屬於 ArduPilot 專案；Realtek SDK 裡的問題屬於 Realtek。

## 已知的限制

下列是本移植已寫明的行為，列在 [Wi-Fi](/libraries/AP_HAL_RTL8735B/docs/wifi.zh-TW.md#安全上的限制) 與[經 Wi-Fi 更新](/libraries/AP_HAL_RTL8735B/docs/flashing.zh-TW.md#安全上的限制)的安全上的限制：

- Wi-Fi 上的 MAVLink 沒有驗證。UDP：還沒有地面站、或地面站超過 3 秒沒送東西時，任何送封包到開發板埠的來源都會被當成新的地面站，它的 RC override 也會被接受。TCP：地面站超過 3 秒沒送東西後，同網段任一主機的新連線就會取代它。
- 沒有地面站時，HEARTBEAT 廣播到整個子網路。
- Wi-Fi 帳密以常數陣列編進映像；拿到映像的人可能讀得出來。
- 沒有設定檔時，開發板連[預設熱點](/libraries/AP_HAL_RTL8735B/docs/wifi.zh-TW.md#預設熱點)。
- 經 Wi-Fi 更新（開發板的應用程式 `app/ota`）會驗每份映像的簽章，但傳輸是明文 TCP、沒有 nonce：同網段有簽好的 `.ota` 檔、或錄下傳送過程的人，可以在開發板未解鎖時重送，包括較舊的映像。
- `generate_keys.py` 產生的私鑰簽的映像，每塊以對應公鑰建置的開發板都接受；私鑰不放進儲存庫、不編進韌體。

超出這些的方法，例如不需要映像檔就讀出帳密，或從子網路以外影響開發板，照上面的方法私下回報。要改變其中某個行為的提議，例如加上驗證，開公開的 [issue](https://github.com/rainwu0/ArduPilot-RTL8735B/issues)。
