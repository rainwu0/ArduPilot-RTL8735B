# Flash 版面

[English](flash-layout.md)

`flash_ntz.bin` 裡的分割表（韌體槽、ISP IQ、NN 模型區等的位址與長度），以及參數存在哪裡。

## `FLASH_LAYOUT`

版面由 hwdef 的 `FLASH_LAYOUT` 決定，以 `--extra-hwdef` 疊加：

```text
FLASH_LAYOUT sdk-d1b6426
```

| 版面 | fw1 | fw2 | 映像上限 | 來源 |
|---|---|---|---|---|
| `realtek-ota`（預設） | 0x060000，4 MiB | 0x520000，4 MiB | 4,194,304 bytes | Realtek Arduino 套件 OTA Mode=Enable 的 `amebapro2_partitiontable_OTA.json`（Ameba-AIoT/ameba-arduino-pro2 `93d6351`），與 Ameba-AIoT/ameba-rtos-pro2 `eb5c090` 的範例分割相同 |
| `sdk-d1b6426` | 0x080000，3.5 MiB | 0x4C0000，3.5 MiB | 3,670,016 bytes | ambpro2_sdk `d1b6426` 範例專案的 `amebapro2_partitiontable.json` |
| `arduino-default` | 0x060000，4 MiB | 0x520000，64 KiB | 4,194,304 bytes | 同一個 Arduino 套件 OTA Mode=Disable 的 `amebapro2_partitiontable.json` |

- 各版面的現況見[功能狀態](feature-status.zh-TW.md)。
- 燒錄預設建置的 `flash_ntz.bin` 時，開發板的分割表會換成 `realtek-ota`。HUB 8735 ultra（ideashatch/HUB-8735 `870a7e0` 發行的板商套件 4.1.1）與 AMB82-mini（Ameba-AIoT/ameba-arduino-pro2 `93d6351`）的 Arduino 套件在 OTA Mode=Enable 時，以及新版 SDK（Ameba-AIoT/ameba-rtos-pro2 `eb5c090`），都用這個版面。`sdk-d1b6426` 沿用 SDK `d1b6426` 原本的分割。
- 建置時以 SDK 的分割表為底，只改寫各記錄的位址與長度。
- 映像上限是 fw1 的長度，因為韌體燒在 fw1。`firmware_ntz.bin` 超過上限，或建出的 `flash_ntz.bin` 的槽記錄與版面不同時，建置失敗。
- 本儲存庫不提供 Wi-Fi 韌體更新，不會寫入 fw2；fw2 只是分割表中的一筆記錄。
- 版面在 `./waf configure` 時讀進來：改了 hwdef 或疊加檔的 `FLASH_LAYOUT` 之後要重新 configure。

## 參數儲存

- 參數存在 0xF80000–0xFA0000，三種版面都相同。Arduino 套件（`93d6351`）與新版 SDK（`eb5c090`）的 NOR 檔案系統區（0xF64000–0x1000000）涵蓋這一段，只在啟用 Flash 檔案系統時會衝突；本移植沒有啟用。
- 參數用 ArduPilot 的 `AP_FlashStorage` 存在兩個 64 KiB 扇區之間輪替。寫到一半斷電，只會丟掉最後那一筆。
- 一個扇區寫滿、換到另一個之後，下一次開機會抹除寫滿的那個（16 次 4 KiB 抹除）。這段時間斷電或按 reset，再開機時參數會全部回到預設，上鎖前檢查會看到校正值遺失。用地面站存的參數檔可以寫回。
- 執行中也可能抹除（同一次開機裡第二次換扇區，或寫入失敗後重建參數區），只在上鎖時開始。抹除進行中，解鎖前檢查報 `Param storage failed`，一般解鎖被擋，抹完自動恢復；強制解鎖不經這項檢查。

抹除期間這段空窗的原因，見[設計說明](design.zh-TW.md#參數儲存與斷電)。
