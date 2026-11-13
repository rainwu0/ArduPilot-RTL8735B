# 燒錄韌體

[English](flashing.md)

怎麼把韌體寫進 RTL8735B 開發板：經 USB 序列埠寫入 `flash_ntz.bin`，在 Windows 上手動燒錄，或在建置最後以 `--upload` 燒錄；或經 Wi-Fi 把簽好的映像送給執行開發板應用程式 `app/ota` 的開發板。建置映像見[建置韌體](building.zh-TW.md)。

## 燒錄模式

重置時 PA5 的電位決定晶片是否進入燒錄模式。各開發板有自己的按鍵；HUB 8735 ultra 見它 README 的[燒錄韌體](../hwdef/hub8735ultra/README.zh-TW.md#燒錄韌體)。

進入燒錄模式後就要燒錄。隔太久工具會報 `ping fail` 或 `ucfg fail`，重新進入燒錄模式即可。

## 在 Windows 上手動燒錄

工具在 Windows 上執行，因為 WSL2 預設看不到 USB 序列埠。

1. 解開 SDK 的 `tools/Pro2_PG_tool _v1.3.0.zip`（檔名含空白），裡面有 `uartfwburn.exe`。
2. 開發板的 USB 序列埠接上電腦，在裝置管理員查出它的 COM 埠。
3. 關掉開著這個 COM 埠的程式，例如地面站軟體與序列終端機，或改用網路連線。
4. 讓晶片進入燒錄模式。
5. 執行工具，`-p` 換成實際的埠，`-f` 後面用絕對路徑：

   ```powershell
   .\uartfwburn.exe -p COM7 -f <路徑>\flash_ntz.bin -b 2000000 -U
   ```

6. 看到 `download success` 後，重置一次開發板即開機。

工具跑完之前 USB 保持接著。SDK 另附 Linux 版 `uartfwburn.linux`。

## 以 `--upload` 燒錄

建置指令加上 `--upload` 與 `--upload-port`，建置完成後會以 SDK 附的 `uartfwburn`（`tools/Pro2_PG_tool _v1.3.0.zip`）燒錄 `flash_ntz.bin`：

```bash
./waf copter --upload --upload-port COM7          # WSL：經 interop 執行 Windows 版工具
./waf copter --upload --upload-port /dev/ttyUSB0  # Linux：執行 Linux 版工具
```

- 建置印出「請讓開發板進入燒錄模式」時，讓晶片進入燒錄模式。60 秒內沒有進入會自動重試，逾時則不寫入。看到 `download success` 後重置一次即開機。
- `COMn` 需要 WSL interop（能在 WSL 執行 Windows 程式），沒有時 `--upload` 會在編譯開始前提示。開了 systemd 的 WSL 在發行版閒置關閉、又在同一個 WSL VM 內重新啟動後，interop 可能不會重新註冊：在 Windows 執行 `wsl --shutdown` 讓 WSL 重新開機；建置與燒錄期間開著一個 WSL 終端機，發行版就不會關閉。
- `/dev/ttyXXX` 需要該裝置的讀寫權限（`dialout` 群組）；在 WSL 要先以 usbipd 把 USB 序列裝置掛進 WSL。
- 環境變數 `AP_OVERRIDE_UPLOAD_CMD` 可改用自訂指令：執行時後面接上映像路徑，有 `--upload-port` 時再接 `--port <埠>`。
- 只下載、不抹除；燒完不會自動重置。

## 經 Wi-Fi 更新

開發板的應用程式 `app/ota` 經 Wi-Fi 收簽好的映像，寫進另一個韌體槽，再以試開的方式開機。新映像自己確認之後保留；沒有確認時，開發板退回原來的槽。

第一份映像照[為經 Wi-Fi 更新建置](building.zh-TW.md#為經-wi-fi-更新建置)建好，先照上面經 USB 序列埠燒錄一次。

### 送出映像

經 Wi-Fi 送的每份映像，都照第一份的方式建置：有 `app/ota`、同一把公鑰、同一個 `FLASH_LAYOUT`。然後簽章、送出：

```bash
python3 Tools/scripts/rtl8735b_ota_sign.py --private-key ~/mykey_private_key.dat build/<板型>/sdk_build/firmware_ntz.bin copter.ota
python3 Tools/scripts/rtl8735b_ota_send.py --host <開發板 IP> copter.ota
python3 Tools/scripts/rtl8735b_ota_send.py --host <開發板 IP> --probe
```

- `rtl8735b_ota_sign.py` 簽的是 `firmware_ntz.bin`，不是 `flash_ntz.bin`，需要 `pymonocypher` 3.1.3.2。加 `--no-reboot` 時，開發板寫完映像不重開。
- `rtl8735b_ota_send.py` 不需要金鑰。連線之前先核對檔案的格式、長度、雜湊與位元組總和；加 `--public-key <檔案>` 時另外驗簽章。開發板的 IP 在地面站收到的 `WiFi: up, local <IP>:<port>`。`setup()` 完成、Wi-Fi 連上之後，開發板在 TCP 埠 8266 等連線；`setup()` 停住時（例如停在 `Config Error`）不等連線。沒有感測器的開發板，用 [`examples/AppHost`](board-applications.zh-TW.md#examplesapphost) 加 `app/ota` 建置，就有接收端。
- 映像寫好時印出 `OTA 完成：N bytes 已寫入另一個韌體槽…`，開發板未解鎖時重開進新映像。
- `--probe` 不寫入任何東西，回報開發板從哪一槽開機：`fwN 已確認（不在試開中）` 或 `fwN 還在試開、等確認`。

其他建法的映像：

- 沒有 `app/ota` 的映像：開機後沒有試開、沒有接收端；要退回得經 USB 序列埠燒錄。
- 沒有公鑰、或 `FLASH_LAYOUT` 不同的映像：不會確認，開發板在試開三次後退回。

### 開發板怎麼處理

- 下列情況拒收：已解鎖時，不回應就關閉連線；目前的映像還在試開（`busy`）；簽章和它的公鑰對不上；分割表和韌體的 `FLASH_LAYOUT` 不同；映像超過 [Flash 版面](flash-layout.zh-TW.md)的上限。簽章驗過之前不碰 Flash。
- 映像邊收邊寫進另一槽，從第一塊起，那一槽原本的映像就被蓋掉。收完比對映像的雜湊和簽過的雜湊，再讀回核對，都通過才啟用新的槽。雜湊不同或連線中斷時，另一槽沒有有效的映像，執行中的槽不變。
- 傳送期間，每次抹除 4 KiB 時主迴圈停住，到抹除完成為止；一般解鎖被拒，見[開發板的應用程式](board-applications.zh-TW.md#韌體呼叫的函式)。Copter 以外的載具可以用 `ARMING_SKIPCHK` 略過這項檢查，強制解鎖在各載具都不經這項檢查；解鎖之後，開發板最多再寫一塊 1 KiB、抹一個 4 KiB 就停止。
- 新映像第一次開機起進入試開，每次開機算一次試開。開機滿 60 秒、主迴圈在跑、接收端帶著公鑰在等連線、Wi-Fi 連上時，映像自己確認。確認之前，一般解鎖被拒，開發板也拒收下一份映像。
- 開機 300 秒後還在試開的映像會被重開：解鎖中不重開，上鎖後再等 60 秒。讓排程器停住的當機（例如 HardFault）不會被重開；每拔插一次電源算一次試開。
- 試開三次都沒有確認時，開發板在開機時退回原來的槽。

### 只能經 USB 序列埠救回的情況

下列情況只能經 USB 序列埠燒錄救回：

- 新映像在開機時、開發板的應用程式啟動之前就當機或卡住：不算試開次數。
- 分割表和韌體的版面不同、槽也放不進 Flash，目前的映像又在試開：一直拒絕解鎖與更新。
- `FLASH_LAYOUT` 不同的映像用完試開次數，另一槽又沒有有效的映像：它繼續執行，拒絕解鎖與更新。
- 開機時寫不了試開次數，目前的映像又在試開：一直拒絕解鎖與更新。
- 執行中的映像沒有 `app/ota`。

### 安全上的限制

- 映像有簽章，但傳輸是明文 TCP，沒有 nonce。同網段有簽好的 `.ota` 檔、或錄下傳送過程的人，可以在開發板未解鎖時重送：蓋掉另一槽、反覆佔住接收端（期間一般解鎖被拒），或裝回較舊的已簽映像。沒有防止降版。
- 簽好的 `.ota` 檔和私鑰一樣不公開。
- 私鑰簽的映像，每塊以對應公鑰建置的開發板都接受。私鑰不放進儲存庫、不編進韌體，建置也拒收私鑰檔。私鑰遺失時，這些開發板只能經 USB 序列埠換韌體。
- 試開探測不需要簽章：同網段任何人都讀得到開發板從哪一槽執行、確認了沒有。
- Wi-Fi 子網路就是信任邊界，和 [Wi-Fi](wifi.zh-TW.md#安全上的限制) 的 MAVLink 相同。

手動燒錄、`--upload` 與經 Wi-Fi 更新的現況見[功能狀態](feature-status.zh-TW.md)。
