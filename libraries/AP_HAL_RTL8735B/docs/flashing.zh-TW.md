# 燒錄韌體

[English](flashing.md)

怎麼經 USB 序列埠把 `flash_ntz.bin` 寫進 RTL8735B 開發板：在 Windows 上手動燒錄，或在建置最後以 `--upload` 燒錄。建置映像見[建置韌體](building.zh-TW.md)。

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

手動燒錄與 `--upload` 的現況見[功能狀態](feature-status.zh-TW.md)。
