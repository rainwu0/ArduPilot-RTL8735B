# SD 卡

[English](sd-card.md)

板上的 microSD 卡經 FatFs 讀寫（`libraries/AP_Filesystem/AP_Filesystem_RTL8735B.cpp`），地面站的 MAVLink FTP 看得到卡上的檔案。

## 設定

- 板型 `rtl8735b-empty` 打開（hwdef 的 `define HAL_RTL8735B_SDCARD_ENABLED 1`），include 它的 `hub8735ultra`、`amb82-mini` 也一樣。
- 沒有 SD 卡座的自訂板，在疊加檔以 `undef HAL_RTL8735B_SDCARD_ENABLED` 關掉。

## 卡與掛載

- 卡先在電腦上格式化成 FAT32 或 exFAT。卡上沒有檔案系統時，也可以從地面站送 MAVLink 的 `MAV_CMD_STORAGE_FORMAT` 格式化，整張卡的資料會清掉。
- 開機後第一次存取檔案時掛載。沒插卡時每 3 秒最多重試一次，插卡後下一次存取就會掛上。
- SD 訊號固定 3.3 V，不用 1.8 V 的高速模式。

## 記錄

檔案系統記錄有編進韌體，但預設不寫（`LOG_BACKEND_TYPE` 預設 0）。`LOG_BACKEND_TYPE` 設成 1 時，記錄寫在卡上的 `/APM/LOGS`。設了之後沒插卡，解鎖前檢查會報 `Logging failed` 或 `No SD card`。

## FatFs

FatFs 在 `fatfs/`：R0.14b，取自 ArduPilot 的 ChibiOS 子模組（`modules/ChibiOS` 固定的提交 `9aebaf4a40` 的 `ext/fatfs`），內容與 ChaN 發行的 R0.14b 相同。`fatfs/ffconf.h` 是本平台的設定，取自 ArduPilot 的 `libraries/AP_HAL_ChibiOS/hwdef/common/ffconf.h`。

SD 卡功能的狀態見[功能狀態](feature-status.zh-TW.md)。
